#ifndef tp_http_Client_h
#define tp_http_Client_h

#include "tp_http/Globals.h"

namespace tp_http
{

class Request;

//##################################################################################################
class Client
{
public:
  //################################################################################################
  Client();

  //################################################################################################
  ~Client();

  //################################################################################################
  void sendRequest(Request* request);

private:
  struct Private;
  friend struct Private;
  Private* d;
};

}

#endif
