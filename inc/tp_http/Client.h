#ifndef tp_http_Client_h
#define tp_http_Client_h

#include "tp_http/Globals.h" // IWYU pragma: keep

#include <boost/asio/io_context.hpp>

namespace tp_http
{

class Request;

//##################################################################################################
enum class Priority : uint32_t
{
  Low    = 1,
  Medium = 2,
  High   = 3,
};

//##################################################################################################
//! This Client class is used to queue up http requests.
class Client
{
  TP_NONCOPYABLE(Client);
  TP_DQ;
public:
  //################################################################################################
  Client(size_t maxInFlight=4, size_t nThreads=1);

  //################################################################################################
  ~Client();

  //################################################################################################
  //! Add a request to the queue to be processed, this takes ownership.
  void sendRequest(Request* request, Priority priority=Priority::Low);

  //################################################################################################
  //! Returns the number of requests in the queue as well as those inFlight.
  size_t pending() const;

  //################################################################################################
  //! Returns the number of requests that have been started but not yet completed.
  size_t inFlight() const;

  //################################################################################################
  size_t bpsDownloaded() const;

  //################################################################################################
  size_t bpsUploaded() const;

  //################################################################################################
  boost::asio::io_context* ioc();
};

}

#endif
