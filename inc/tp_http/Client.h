#ifndef tp_http_Client_h
#define tp_http_Client_h

#include "tp_http/Globals.h"

namespace tp_http
{

class Request;

//##################################################################################################
//! This Client class is used to queue up http requests.
class Client
{
public:
  //################################################################################################
  Client(size_t maxInFlight=4);

  //################################################################################################
  ~Client();

  //################################################################################################
  //! Add a request to the queue to be processed, this takes ownership.
  void sendRequest(Request* request);

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
