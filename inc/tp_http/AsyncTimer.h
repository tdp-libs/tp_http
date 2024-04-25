#ifndef tp_http_AsyncTimer_h
#define tp_http_AsyncTimer_h

#include "tp_http/Globals.h" // IWYU pragma: keep

#include <boost/asio/io_context.hpp>

namespace tp_http
{

//##################################################################################################
//! This Client class is used to queue up http requests.
class AsyncTimer
{
  TP_NONCOPYABLE(AsyncTimer);
  TP_DQ;
public:
  //################################################################################################
  AsyncTimer(const std::function<void()>& callback, int64_t timeoutMS, boost::asio::io_context* ioContext);

  //################################################################################################
  ~AsyncTimer();
};

}

#endif
