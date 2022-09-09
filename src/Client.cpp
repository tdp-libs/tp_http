#include "tp_http/Client.h"
#include "tp_http/Request.h"

#include "tp_utils/MutexUtils.h"
#include "tp_utils/DebugUtils.h"
#include "tp_utils/RefCount.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ssl.hpp>

#include <queue>
#include <memory>
#include <thread>

namespace tp_http
{
namespace
{
struct SocketDetails_lt;

//##################################################################################################
// This is used to handle timer events that arrive after the message has been deleted.
struct Handle_lt
{
  TP_REF_COUNT_OBJECTS("tp_http::Handle_lt");
  std::mutex mutex;
  SocketDetails_lt* s;
  Handle_lt(SocketDetails_lt* s_):s(s_){}
};

//##################################################################################################
struct SocketDetails_lt
{
  TP_REF_COUNT_OBJECTS("tp_http::SocketDetails_lt");

  Request* r;
  boost::beast::http::serializer<true,boost::beast::http::string_body>* serializer{nullptr};
  const std::function<void()> completed;

  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket socket;

  boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> sslSocket;

  boost::asio::deadline_timer deadlineTimer;
  boost::beast::flat_buffer buffer;

  std::shared_ptr<Handle_lt> handle;

  //################################################################################################
  SocketDetails_lt(boost::asio::io_context& ioContext, const std::shared_ptr<boost::asio::ssl::context>& sslCtx, const std::function<void()>& completed_):
    completed(completed_),
    resolver(ioContext),
    socket(ioContext),
    sslSocket(socket, *sslCtx),
    deadlineTimer(ioContext),
    handle(new Handle_lt(this))
  {

  }

  //################################################################################################
  ~SocketDetails_lt()
  {
    {
      std::lock_guard<std::mutex> lock(handle->mutex);(void)lock;
      handle->s = nullptr;
    }

    deadlineTimer.cancel();
    delete serializer;
    delete r;

    completed();
  }

  //################################################################################################
  void checkTimeout()
  {
    if(deadlineTimer.expires_at() <= boost::asio::deadline_timer::traits_type::now())
    {
      tpWarning() << "Timeout reached.....";
      boost::system::error_code ec;
      r->fail(ec, "Timeout reached.....");
      socket.close();
    }
  }

  //################################################################################################
  void setTimeout(int timeout)
  {
    deadlineTimer.expires_from_now(boost::posix_time::seconds(timeout));
    auto handle=this->handle;
    deadlineTimer.async_wait([&, handle](const boost::system::error_code& ec)
    {
      std::lock_guard<std::mutex> lock(handle->mutex);(void)lock;
      if(handle->s && !ec)
        checkTimeout();
    });
  }

  //################################################################################################
  void clearTimeout()
  {
    deadlineTimer.expires_at(boost::posix_time::pos_infin);
    deadlineTimer.cancel();
  }
};
}

//##################################################################################################
struct Client::Private
{
  TP_REF_COUNT_OBJECTS("tp_http::Client::Private");

  const size_t maxInFlight;

  std::shared_ptr<boost::asio::ssl::context> sslCtx;
  boost::asio::io_context ioContext;
  std::unique_ptr<boost::asio::io_context::work> work;
  std::vector<std::thread*> threads;

  TPMutex requestQueueMutex{TPM};
  std::queue<Request*> requestQueue;
  size_t inFlight{0};

  //################################################################################################
  Private(size_t maxInFlight_):
    maxInFlight(maxInFlight_),
    sslCtx(makeCTX()),
    work(std::make_unique<boost::asio::io_context::work>(ioContext))
  {
    for(int i=0; i<1; i++)
      threads.push_back(new std::thread([&]{ioContext.run();}));
  }

  //################################################################################################
  ~Private()
  {
    work.reset();
    ioContext.stop();
    {
      //The order here is important, we need to clear empty while messageQueueMutex is unlocked.
      std::queue<Request*> empty;
      {
        TP_MUTEX_LOCKER(requestQueueMutex);
        requestQueue.swap(empty);
      }

      while(!empty.empty())
      {
        delete empty.front();
        empty.pop();
      }
    }

    for(const auto& thread : threads)
    {
      thread->join();
      delete thread;
    }
  }

  //################################################################################################
  static std::shared_ptr<boost::asio::ssl::context> makeCTX()
  {
    auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
    addSSLVerifyPaths(*ctx);
    return ctx;
  }

  //################################################################################################
  std::shared_ptr<SocketDetails_lt> postNext()
  {
    if(inFlight>=maxInFlight)
      return std::shared_ptr<SocketDetails_lt>();

    if(requestQueue.empty())
      return std::shared_ptr<SocketDetails_lt>();

    inFlight++;

    auto s = std::make_shared<SocketDetails_lt>(ioContext, sslCtx, [&]
    {
      std::shared_ptr<SocketDetails_lt> s;
      TP_MUTEX_LOCKER(requestQueueMutex);
      inFlight--;
      s = postNext();
    });
    s->r = requestQueue.front();
    requestQueue.pop();

    run(s);
    return s;
  }

  //################################################################################################
  // Start the asynchronous http request
  void run(const std::shared_ptr<SocketDetails_lt>& s)
  {
    try
    {
      // Look up the domain name
      s->resolver.async_resolve(s->r->host(),
                                std::to_string(s->r->port()),
                                [this, s](const boost::system::error_code& ec, const boost::asio::ip::tcp::resolver::results_type& results)
      {
        onResolve(s, ec, results);
      });
    }
    catch(...)
    {
      boost::system::error_code ec;
      return s->r->fail(ec, "async_resolve exception");
    }
  }

  //################################################################################################
  // Called once the IP address has been resolved.
  void onResolve(const std::shared_ptr<SocketDetails_lt>& s,
                 const boost::system::error_code& ec,
                 const boost::asio::ip::tcp::resolver::results_type& results)
  {
    if(ec)
      return s->r->fail(ec, "resolve");

    s->setTimeout(30);
    s->r->setProgress(0.05f);

    try
    {
      boost::asio::async_connect(s->socket,
                                 results.begin(),
                                 results.end(),
                                 [this, s](const boost::system::error_code& ec, const boost::asio::ip::tcp::resolver::iterator& iterator)
      {
        if(!ec)
        {
          const int timeout = 240 * 1000;
          ::setsockopt(s->socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
          ::setsockopt(s->socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
        }


        s->clearTimeout();
        onConnect(s, ec, iterator);
      });
    }
    catch(...)
    {
      return s->r->fail(ec, "async_connect exception");
    }
  }

  //################################################################################################
  // Send the HTTP request or HTTPS handshake to the remote host.
  void onConnect(const std::shared_ptr<SocketDetails_lt>& s,
                 const boost::system::error_code& ec,
                 const boost::asio::ip::tcp::resolver::iterator& iterator)
  {
    (void)iterator;

    if(ec)
      return s->r->fail(ec, "connect");

    s->r->setProgress(0.10f);

    // If this is HTTP we can just get on and send the request, else if it is HTTPS we need to
    // perform a handshake first.
    if(s->r->protocol() == Protocol::HTTP)
    {
      asyncWrite(s, ec);
    }
    else
    {
      try
      {
        s->setTimeout(30);

        s->sslSocket.lowest_layer().set_option(boost::asio::ip::tcp::no_delay(true));
        s->sslSocket.set_verify_mode(boost::asio::ssl::verify_peer);
        //s->sslSocket.set_verify_callback(boost::asio::ssl::rfc2818_verification(s->r->host()));

#ifdef TP_HTTP_VERBOSE
        tpWarning() << "Host name: " << s->r->host();
#endif

#ifndef TP_WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
        if(!SSL_set_tlsext_host_name(s->sslSocket.native_handle(), const_cast<void*>(static_cast<const void*>(s->r->host().data()))))
        {
          return s->r->fail(ec, "SSL_set_tlsext_host_name failed");
        }
#ifndef TP_WIN32
#pragma GCC diagnostic pop
#endif

#ifdef TP_HTTP_VERBOSE
        s->sslSocket.set_verify_callback([](bool preverified, boost::asio::ssl::verify_context& ctx)
        {
          // The verify callback can be used to check whether the certificate that is
          // being presented is valid for the peer. For example, RFC 2818 describes
          // the steps involved in doing this for HTTPS. Consult the OpenSSL
          // documentation for more details. Note that the callback is called once
          // for each certificate in the certificate chain, starting from the root
          // certificate authority.

          // In this example we will simply print the certificate's subject name.
          char subject_name[256];
          X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
          X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
          tpWarning() << "Verifying " << subject_name;

          return preverified;
        });
#endif

        s->sslSocket.async_handshake(boost::asio::ssl::stream_base::client,
                                     [this, s](const boost::system::error_code& ec)
        {
          s->clearTimeout();
          onHandshake(s, ec);
        });
      }
      catch(...)
      {
        return s->r->fail(ec, "async_handshake exception");
      }
    }
  }


  //################################################################################################
  // Send the HTTPS request to the remote host now that handshake has been performed.
  void onHandshake(const std::shared_ptr<SocketDetails_lt>& s,
                   const boost::system::error_code& ec)
  {
    if(ec)
      return s->r->fail(ec, "handshake");

    s->r->setProgress(0.15f);

    asyncWrite(s, ec);
  }

  //################################################################################################
  void asyncWriteSome(const std::shared_ptr<SocketDetails_lt>& s,
                      size_t totalSize,
                      size_t totalSent,
                      const boost::system::error_code& ec)
  {
    s->setTimeout(60);

    try
    {
      auto handler = [this, s, totalSize, totalSent](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        size_t t = totalSent + bytesTransferred;
        if(totalSize>0)
        {
          float f = float(t) / float(totalSize);
          f*=0.4f;
          f+=0.2f;
          s->r->setProgress(f);
        }

        if(s->serializer->is_done())
        {
          s->clearTimeout();
          onWrite(s, ec);
        }
        else
        {
          asyncWriteSome(s, totalSize, t, ec);
        }
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_write_some(s->socket, *s->serializer, handler);
      else
        boost::beast::http::async_write_some(s->sslSocket, *s->serializer, handler);
    }
    catch(...)
    {
      return s->r->fail(ec, "async_write exception");
    }
  }

  //################################################################################################
  void asyncWrite(const std::shared_ptr<SocketDetails_lt>& s,
                  const boost::system::error_code& ec)
  {
    s->r->generateRequest();
    s->r->setProgress(0.20f);

    try
    {
      size_t totalSize=0;
      auto v=s->r->request().payload_size();
      if(v)
        totalSize = *v;

      if(v && (*v)<524288)
      {
        auto handler = [this, s](const boost::system::error_code& ec, size_t bytesTransferred)
        {
          TP_UNUSED(bytesTransferred);
          s->clearTimeout();
          onWrite(s, ec);
        };

        if(s->r->protocol() == Protocol::HTTP)
          boost::beast::http::async_write(s->socket, s->r->request(), handler);
        else
          boost::beast::http::async_write(s->sslSocket, s->r->request(), handler);
      }
      else
      {
        s->serializer = new boost::beast::http::serializer<true,boost::beast::http::string_body>(s->r->request());
        asyncWriteSome(s, totalSize, 0, ec);
      }
    }
    catch(...)
    {
      return s->r->fail(ec, "async_write exception");
    }
  }



  //################################################################################################
  void onWrite(const std::shared_ptr<SocketDetails_lt>& s,
               const boost::system::error_code& ec)
  {
    if(ec)
      return s->r->fail(ec, "write");
    s->r->setProgress(0.60f);


    try
    {


#if 0
      s->setTimeout(240);
      auto handler = [this, s](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        s->clearTimeout();
        onRead(s, ec, bytesTransferred);
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_read(s->socket, s->buffer, s->r->mutableParser(), handler);
      else
        boost::beast::http::async_read(s->sslSocket, s->buffer, s->r->mutableParser(), handler);
#else
      s->setTimeout(60);
      auto handler = [this, s](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        onReadSome(s, ec, bytesTransferred);
      };
      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_read_some(s->socket, s->buffer, s->r->mutableParser(), handler);
      else
        boost::beast::http::async_read_some(s->sslSocket, s->buffer, s->r->mutableParser(), handler);
#endif
    }
    catch(...)
    {
      return s->r->fail(ec, "async_read exception");
    }
  }

  //################################################################################################
  void onReadSome(const std::shared_ptr<SocketDetails_lt>& s,
                  const boost::system::error_code& ec,
                  size_t bytesTransferred)
  {
    if(!ec && !s->r->mutableParser().is_done())
    {
      s->setTimeout(60);

      auto handler = [this, s](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        onReadSome(s, ec, bytesTransferred);
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_read_some(s->socket, s->buffer, s->r->mutableParser(), handler);
      else
        boost::beast::http::async_read_some(s->sslSocket, s->buffer, s->r->mutableParser(), handler);
    }
    else
    {
      s->clearTimeout();
      onRead(s, ec, bytesTransferred);
    }
  }

  //################################################################################################
  void onRead(const std::shared_ptr<SocketDetails_lt>& s,
              const boost::system::error_code& ec,
              size_t bytesTransferred)
  {
    TP_UNUSED(bytesTransferred);

    if(ec)
      return s->r->fail(ec, "read");

    s->r->setCompleted();
    s->r->setProgress(1.0f);

    {
      boost::system::error_code ec;
      try
      {
        s->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
      }
      catch(...)
      {
        return s->r->fail(ec, "shutdown exception");
      }

      if(ec && ec != boost::system::errc::not_connected)
        return s->r->fail(ec, "shutdown");
    }
  }
};

//##################################################################################################
Client::Client(size_t maxInFlight):
  d(new Private(maxInFlight))
{

}

//##################################################################################################
Client::~Client()
{
  delete d;
}

//##################################################################################################
void Client::sendRequest(Request* request)
{
  request->setAddedToClient();
  std::shared_ptr<SocketDetails_lt> s;
  TP_MUTEX_LOCKER(d->requestQueueMutex);
  d->requestQueue.emplace(request);
  s = d->postNext();
}

//##################################################################################################
size_t Client::pending() const
{
  TP_MUTEX_LOCKER(d->requestQueueMutex);
  return d->requestQueue.size() + d->inFlight;
}

//##################################################################################################
size_t Client::inFlight() const
{
  TP_MUTEX_LOCKER(d->requestQueueMutex);
  return d->inFlight;
}

}
