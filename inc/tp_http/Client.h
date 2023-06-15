#ifndef tp_http_Client_h
#define tp_http_Client_h

#include "tp_http/Globals.h"

namespace tp_http
{

class Request;

//##################################################################################################
enum class Priority
{
  Low    = 1,
  Medium = 2,
  High   = 3
};

//##################################################################################################
//! This Client class is used to queue up http requests.
class Client
{
  TP_NONCOPYABLE(Client);
public:
  //################################################################################################
  Client(size_t maxInFlight=4);

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

private:
  struct Private;
  friend struct Private;
  Private* d;
};

}

#endif
