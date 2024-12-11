#include "tp_http/Client.h"
#include "tp_http/Request.h"
#include "tp_http/ResolverResults.h"
#include "tp_http/AsyncTimer.h"

#include "tp_utils/MutexUtils.h"
#include "tp_utils/DebugUtils.h"
#include "tp_utils/RefCount.h"
#include "tp_utils/TimeUtils.h"

#include "lib_platform/SetThreadName.h"

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

  size_t uploadSize{0};
  size_t downloadSize{0};

  //################################################################################################
  SocketDetails_lt(boost::asio::io_context& ioContext, const std::shared_ptr<boost::asio::ssl::context>& sslCtx, const std::function<void()>& completed_):
    completed(completed_),
    resolver(ioContext),
    socket(ioContext),
    sslSocket(socket, *sslCtx),
    deadlineTimer(ioContext),
    handle(new Handle_lt(this))
  {
    buffer.max_size(16384);
    buffer.prepare(16384);
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
  void setTimeout(int timeout)
  {
    deadlineTimer.expires_from_now(boost::posix_time::seconds(timeout));
    auto handle=this->handle;
    deadlineTimer.async_wait([&, handle](const boost::system::error_code& ec)
    {
      std::lock_guard<std::mutex> lock(handle->mutex);(void)lock;
      if(handle->s && !ec)
      {
        if(deadlineTimer.expires_at() <= boost::asio::deadline_timer::traits_type::now())
        {
          tpWarning() << "Timeout reached.....";
          boost::system::error_code ec;
          r->fail(ec, "Timeout reached.....");

          if(socket.is_open())
          {
            try
            {
              handle->s = nullptr;
              socket.close();
            }
            catch(...)
            {

            }
          }
        }
      }
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
  std::unique_ptr<boost::asio::io_context> ioContext;
  std::unique_ptr<boost::asio::io_context::work> work;
  std::vector<std::thread*> threads;

  uint32_t priorityMultiplier{10};
  bool incrementPriority{true};

  TPMutex requestQueueMutex{TPM};
  std::deque<std::pair<Request*, uint32_t>> requestQueue;
  size_t inFlight{0};

  bool useDNSCache{true};
  TPMutex dnsCacheMutex{TPM};
  std::map<std::string, boost::asio::ip::tcp::resolver::results_type> dnsCache;

  TPMutex statsMutex{TPM};
  size_t bytesDownloaded{0};
  size_t bytesUploaded{0};
  size_t bpsDownloaded{0};
  size_t bpsUploaded{0};

  int64_t lastStatsUpdate{tp_utils::currentTimeMS()};

  //################################################################################################
  Private(size_t maxInFlight_, size_t nThreads):
    maxInFlight(maxInFlight_),
    sslCtx(makeCTX()),
    ioContext(std::make_unique<boost::asio::io_context>()),
    work(std::make_unique<boost::asio::io_context::work>(*ioContext))
  {
    for(size_t i=0; i<nThreads; i++)
      threads.push_back(new std::thread([&]
      {
        lib_platform::setThreadName("Client");
        ioContext->run();
      }));
  }

  //################################################################################################
  ~Private()
  {
    work.reset();
    ioContext->stop();
    {
      //The order here is important, we need to clear empty while requestQueueMutex is unlocked.
      std::deque<std::pair<Request*, uint32_t>> empty;
      {
        TP_MUTEX_LOCKER(requestQueueMutex);
        requestQueue.swap(empty);
      }

      for(const auto& r : empty)
        delete r.first;
    }

    for(const auto& thread : threads)
    {
      thread->join();
      delete thread;
    }

    //ioContext.reset();
  }

  //################################################################################################
  AsyncTimer updateStatsThread = AsyncTimer([&]
  {
    TP_MUTEX_LOCKER(statsMutex);

    int64_t now = tp_utils::currentTimeMS();
    int64_t delta = now - lastStatsUpdate;
    lastStatsUpdate = now;

    bpsDownloaded = (bytesDownloaded*1000) / delta;
    bpsUploaded = (bytesUploaded*1000) / delta;

    bytesDownloaded = 0;
    bytesUploaded = 0;
  }, 1000, ioContext.get());

  //################################################################################################
  static std::shared_ptr<boost::asio::ssl::context> makeCTX()
  {
    auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
    addSSLVerifyPaths(*ctx);
    return ctx;
  }

  //################################################################################################
  void recordBytesDownloaded(size_t bytes)
  {
    TP_MUTEX_LOCKER(statsMutex);
    bytesDownloaded+=bytes;
  }

  //################################################################################################
  void recordBytesUploaded(size_t bytes)
  {
    TP_MUTEX_LOCKER(statsMutex);
    bytesUploaded+=bytes;
  }

  //################################################################################################
  std::shared_ptr<SocketDetails_lt> postNext()
  {
    if(inFlight>=maxInFlight)
      return std::shared_ptr<SocketDetails_lt>();

    if(requestQueue.empty())
      return std::shared_ptr<SocketDetails_lt>();

    inFlight++;

    auto s = std::make_shared<SocketDetails_lt>(*ioContext, sslCtx, [&]
    {
      std::shared_ptr<SocketDetails_lt> s;
      TP_MUTEX_LOCKER(requestQueueMutex);
      inFlight--;

      // Assigning to s is important don't remove it.
      s = postNext();
    });
    s->r = requestQueue.front().first;
    requestQueue.pop_front();

    run(s);
    return s;
  }

  //################################################################################################
  // Start the asynchronous http request
  void run(const std::shared_ptr<SocketDetails_lt>& s)
  {
    if(const auto& fakeAFailure=s->r->fakeAFailure(); fakeAFailure)
    {
      s->r->mutableResult().body() = fakeAFailure->body;
      s->r->mutableResult().result(fakeAFailure->code);

      if(fakeAFailure->completed)
        s->r->setCompleted();

      boost::system::error_code ec;
      return s->r->fail(ec, fakeAFailure->whatFailed);
    }

    if(s->r->resolverResults())
    {
      std::shared_ptr<SocketDetails_lt> ss = s;
      boost::system::error_code ec;
      onResolve(ss, ec, s->r->resolverResults()->resolverResults);
      return;
    }

    if(useDNSCache)
    {
      boost::asio::ip::tcp::resolver::results_type results;
      {
        TP_MUTEX_LOCKER(dnsCacheMutex);
        if(auto i=dnsCache.find(s->r->dnsKey()); i!=dnsCache.end())
          results = i->second;
      }

      if(!results.empty())
      {
        std::shared_ptr<SocketDetails_lt> ss = s;

        auto resolverResults = std::make_shared<ResolverResults>();
        resolverResults->resolverResults = results;
        ss->r->setResolverResults(resolverResults);
        boost::system::error_code ec;
        onResolve(ss, ec, ss->r->resolverResults()->resolverResults);
        return;
      }
    }

    try
    {
      // Look up the domain name
      std::shared_ptr<SocketDetails_lt> ss = s;
      s->resolver.async_resolve(s->r->host(),
                                std::to_string(s->r->port()),
                                [this, ss](const boost::system::error_code& ec, const boost::asio::ip::tcp::resolver::results_type& results)
      {
        if(useDNSCache)
        {
          TP_MUTEX_LOCKER(dnsCacheMutex);
          dnsCache[ss->r->dnsKey()] = results;
        }

        auto resolverResults = std::make_shared<ResolverResults>();
        resolverResults->resolverResults = results;
        ss->r->setResolverResults(resolverResults);

        onResolve(ss, ec, ss->r->resolverResults()->resolverResults);
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
    s->r->setProgress(0.05f, 0, 0);

    try
    {
      std::shared_ptr<SocketDetails_lt> ss = s;

      boost::asio::async_connect(s->socket,
                                 results.begin(),
                                 results.end(),
                                 [this, ss](const boost::system::error_code& ec, const boost::asio::ip::tcp::resolver::iterator& iterator)
      {
        if(!ec)
        {
          const int timeout = 240 * 1000;
          ::setsockopt(ss->socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
          ::setsockopt(ss->socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
        }

        ss->clearTimeout();
        onConnect(ss, ec, iterator);
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

    s->r->setProgress(0.10f, 0, 0);

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

        std::shared_ptr<SocketDetails_lt> ss = s;
        s->sslSocket.async_handshake(boost::asio::ssl::stream_base::client,
                                     [this, ss](const boost::system::error_code& ec)
        {
          ss->clearTimeout();
          onHandshake(ss, ec);
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

    s->r->setProgress(0.15f, 0, 0);

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
      std::shared_ptr<SocketDetails_lt> ss = s;
      auto handler = [this, ss, totalSize, totalSent](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        recordBytesUploaded(bytesTransferred);

        size_t t = totalSent + bytesTransferred;
        if(totalSize>0)
        {
          float f = float(t) / float(totalSize);
          f*=0.4f;
          f+=0.2f;
          ss->r->setProgress(f, ss->uploadSize, ss->downloadSize);
        }

        if(ss->serializer->is_done())
        {
          ss->clearTimeout();
          onWrite(ss, ec);
        }
        else
        {
          asyncWriteSome(ss, totalSize, t, ec);
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

    try
    {
      size_t totalSize=0;
      auto v=s->r->request().payload_size();
      if(v)
        totalSize = *v;

      s->uploadSize = totalSize;
      s->r->setProgress(0.20f, s->uploadSize, s->downloadSize);

      if(v && (*v)<524288)
      {
        std::shared_ptr<SocketDetails_lt> ss = s;
        auto handler = [this, ss](const boost::system::error_code& ec, size_t bytesTransferred)
        {
          recordBytesUploaded(bytesTransferred);
          ss->clearTimeout();
          onWrite(ss, ec);
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
    s->r->setProgress(1.0f, s->uploadSize, s->downloadSize);

    try
    {
#if 0
      s->setTimeout(240);
      std::shared_ptr<SocketDetails_lt> ss = s;
      auto handler = [this, ss](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        ss->clearTimeout();
        onRead(ss, ec, bytesTransferred);
      };

      if(s->r->protocol() == Protocol::HTTP)
        boost::beast::http::async_read(s->socket, s->buffer, s->r->mutableParser(), handler);
      else
        boost::beast::http::async_read(s->sslSocket, s->buffer, s->r->mutableParser(), handler);
#else
      s->setTimeout(1000);
      std::shared_ptr<SocketDetails_lt> ss = s;
      auto handler = [this, ss](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        onReadSome(ss, ec, bytesTransferred);
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
    recordBytesDownloaded(bytesTransferred);

    if(!ec && !s->r->mutableParser().is_done())
    {
      if(s->r->mutableParser().is_header_done())
      {
        auto total = s->r->mutableParser().content_length();
        auto remaining = s->r->mutableParser().content_length_remaining();
        if(total && remaining)
        {
          uint64_t t = *total;
          uint64_t r = *remaining;
          s->downloadSize = size_t(t);
          if(t>=r)
          {
            float f = 1.0f - float(r)/float(t);
            s->r->setProgress(f, s->uploadSize, s->downloadSize);
          }
        }
      }

      s->setTimeout(60);

      std::shared_ptr<SocketDetails_lt> ss = s;
      auto handler = [this, ss](const boost::system::error_code& ec, size_t bytesTransferred)
      {
        onReadSome(ss, ec, bytesTransferred);
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
    recordBytesDownloaded(bytesTransferred);

    if(ec)
      return s->r->fail(ec, "read");

    s->r->setCompleted();
    s->r->setProgress(1.0f, s->uploadSize, s->downloadSize);

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
Client::Client(size_t maxInFlight, size_t nThreads):
  d(new Private(maxInFlight, nThreads))
{

}

//##################################################################################################
Client::~Client()
{
  delete d;
}

//##################################################################################################
void Client::sendRequest(Request* request, Priority priority)
{
  request->setAddedToClient();
  std::shared_ptr<SocketDetails_lt> s;
  TP_MUTEX_LOCKER(d->requestQueueMutex);

  uint32_t p = d->priorityMultiplier * uint32_t(priority);

  if(priority == Priority::Low)
    d->requestQueue.emplace_back(request, p);
  else
  {
    auto i=d->requestQueue.begin();
    while(i!=d->requestQueue.end() && i->second>=p)
      ++i;

    std::pair<Request*, uint32_t> tmp{request, p};

    if(d->incrementPriority)
    {
      for(; i!=d->requestQueue.end(); ++i)
      {
        std::swap(*i, tmp);
        tmp.second++;
      }
    }

    d->requestQueue.emplace(i, tmp);
  }

  // Assigning to s is important don't remove it.
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

//##################################################################################################
size_t Client::bpsDownloaded() const
{
  TP_MUTEX_LOCKER(d->statsMutex);
  return d->bpsDownloaded;
}

//##################################################################################################
size_t Client::bpsUploaded() const
{
  TP_MUTEX_LOCKER(d->statsMutex);
  return d->bpsUploaded;
}

//##################################################################################################
boost::asio::io_context* Client::ioc()
{
  return d->ioContext.get();
}

}
