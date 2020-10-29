#include "tp_http/Client.h"
#include "tp_http/Request.h"

#include "tp_utils/MutexUtils.h"
#include "tp_utils/DebugUtils.h"
#include "tp_utils/StackTrace.h"

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
  std::mutex mutex;
  SocketDetails_lt* s;
  Handle_lt(SocketDetails_lt* s_):s(s_){}
};

//##################################################################################################
struct SocketDetails_lt
{
  Request* r;
  const std::function<void()> completed;

  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket socket;

  boost::asio::ssl::context sslCtx;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> sslSocket;

  boost::asio::deadline_timer deadlineTimer;
  boost::beast::flat_buffer buffer;

  std::shared_ptr<Handle_lt> handle;

  //################################################################################################
  SocketDetails_lt(boost::asio::io_context& ioContext, const std::function<void()>& completed_):
    completed(completed_),
    resolver(ioContext),
    socket(ioContext),
    sslCtx(boost::asio::ssl::context::sslv23),
    sslSocket(socket, sslCtx),
    deadlineTimer(ioContext),
    handle(new Handle_lt(this))
  {
    addSSLVerifyPaths(sslCtx);
  }

  //################################################################################################
  ~SocketDetails_lt()
  {
    {
      std::lock_guard<std::mutex> lock(handle->mutex);(void)lock;
      handle->s = nullptr;
    }

    deadlineTimer.cancel();
    delete r;

    completed();
  }

  //################################################################################################
  void checkTimeout()
  {
    if(deadlineTimer.expires_at() <= boost::asio::deadline_timer::traits_type::now())
    {
      tpDebug() << "Timeout reached.....";
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
  const size_t maxInFlight{4};

  boost::asio::io_context ioContext;
  std::unique_ptr<boost::asio::io_context::work> work;
  std::thread thread;

  TPMutex requestQueueMutex{TPM};
  std::queue<Request*> requestQueue;
  size_t inFlight{0};

  //################################################################################################
  Private():
    work(std::make_unique<boost::asio::io_context::work>(ioContext)),
    thread([&]{ioContext.run();})
  {

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
    thread.join();
  }

  //################################################################################################
  void postNext()
  {
    if(inFlight>=maxInFlight)
      return;

    if(requestQueue.empty())
      return;

    inFlight++;

    auto s = std::make_shared<SocketDetails_lt>(ioContext, [&]
    {
      TP_MUTEX_LOCKER(requestQueueMutex);
      inFlight--;
      postNext();
    });
    s->r = requestQueue.front();
    requestQueue.pop();
    run(s);
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
                                [this, s](boost::system::error_code ec, boost::asio::ip::tcp::resolver::results_type results)
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
                 boost::system::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results)
  {
    if(ec)
      return s->r->fail(ec, "resolve");

    s->setTimeout(30);

    try
    {
      boost::asio::async_connect(s->socket,
                                 results.begin(),
                                 results.end(),
                                 [this, s](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator iterator)
      {
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
                 boost::system::error_code ec,
                 boost::asio::ip::tcp::resolver::iterator iterator)
  {
    (void)iterator;

    if(ec)
      return s->r->fail(ec, "connect");

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

#ifdef TP_LINUX
        if(SSL_ctrl(s->sslSocket.native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<void*>(static_cast<const void*>(s->r->host().data()))))
        {
          return s->r->fail(ec, "SSL_set_tlsext_host_name failed");
        }
#else
        if(!SSL_set_tlsext_host_name(s->sslSocket.native_handle(), const_cast<void*>(static_cast<const void*>(s->r->host().data()))))
        {
          return s->r->fail(ec, "SSL_set_tlsext_host_name failed");
        }
#endif

        s->sslSocket.async_handshake(boost::asio::ssl::stream_base::client,
                                     [this, s](boost::system::error_code ec)
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
                   boost::system::error_code ec)
  {
    if(ec)
      return s->r->fail(ec, "handshake");

    asyncWrite(s, ec);
  }

  //################################################################################################
  void asyncWrite(const std::shared_ptr<SocketDetails_lt>& s,
                  boost::system::error_code ec)
  {
    s->setTimeout(30);
    s->r->generateRequest();

    try
    {
      auto handler = [this, s](boost::system::error_code ec, size_t bytesTransferred)
      {
        s->clearTimeout();
        onWrite(s, ec, bytesTransferred);
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_write(s->socket, s->r->request(), handler);
      else
        boost::beast::http::async_write(s->sslSocket, s->r->request(), handler);
    }
    catch(...)
    {
      return s->r->fail(ec, "async_write exception");
    }
  }

  //################################################################################################
  void onWrite(const std::shared_ptr<SocketDetails_lt>& s,
               boost::system::error_code ec,
               size_t bytesTransferred)
  {
    boost::ignore_unused(bytesTransferred);

    if(ec)
      return s->r->fail(ec, "write");

    s->setTimeout(240);

    try
    {
      auto handler = [this, s](boost::system::error_code ec, size_t bytesTransferred)
      {
        s->clearTimeout();
        onRead(s, ec, bytesTransferred);
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_read(s->socket, s->buffer, s->r->mutableParser(), handler);
      else
        boost::beast::http::async_read(s->sslSocket, s->buffer, s->r->mutableParser(), handler);
    }
    catch(...)
    {
      return s->r->fail(ec, "async_read exception");
    }
  }

  //################################################################################################
  void onRead(const std::shared_ptr<SocketDetails_lt>& s,
              boost::system::error_code ec,
              size_t bytesTransferred)
  {
    TP_UNUSED(bytesTransferred);

    if(ec)
      return s->r->fail(ec, "read");

    s->r->setCompleted();

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
};

//##################################################################################################
Client::Client():
  d(new Private())
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
  TP_MUTEX_LOCKER(d->requestQueueMutex);
  d->requestQueue.emplace(request);
  d->postNext();
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
