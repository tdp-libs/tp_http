#ifndef tp_http_Request_h
#define tp_http_Request_h

#include "tp_http/Globals.h"

#include <boost/beast/http.hpp>

#include <functional>

namespace tp_http
{

//##################################################################################################
class Request
{
public:
  //################################################################################################
  Request(const std::function<void(const Request&)>& completionHandler);

  //################################################################################################
  ~Request();

  //################################################################################################
  void setProtocol(Protocol protocol);

  //################################################################################################
  Protocol protocol() const;

  //################################################################################################
  void setHost(const std::string& host);

  //################################################################################################
  const std::string& host() const;

  //################################################################################################
  void setPort(uint16_t port);

  //################################################################################################
  //! Returns the port number.
  /*!
  Returns the port number, this will be inferred from the protocol if it has not been se expicitly.
  \return The port number.
  */
  uint16_t port() const;

  //################################################################################################
  void setVerb(boost::beast::http::verb verb);

  //################################################################################################
  //! GET, POST, PUT, etc...
  boost::beast::http::verb verb() const;

  //################################################################################################
  void setEndpoint(const std::string& endpoint);

  //################################################################################################
  const std::string& endpoint() const;

  //################################################################################################
  void addHeaderData(const std::string& key, const std::string& value);

  //################################################################################################
  const std::unordered_map<std::string, std::string>& headerData() const;

  //################################################################################################
  void addFormPostData(const std::string& key, const std::string& value);

  //################################################################################################
  const std::unordered_map<std::string, std::string>& formPostData() const;

  //################################################################################################
  void addFormGetData(const std::string& key, const std::string& value);

  //################################################################################################
  const std::unordered_map<std::string, std::string>& formGetData() const;

  //################################################################################################
  void setRawBodyData(const std::string& rawBodyData, const std::string& contentType);

  //################################################################################################
  const std::string& rawBodyData() const;

  //################################################################################################
  const std::string& contentType() const;

  //################################################################################################
  void setBodyEncodeMode(BodyEncodeMode bodyEncodeMode);

  //################################################################################################
  BodyEncodeMode bodyEncodeMode() const;

  //################################################################################################
  void generateRequest();

  //################################################################################################
  const boost::beast::http::request<boost::beast::http::string_body>& request() const;

  //################################################################################################
  const boost::beast::http::response<boost::beast::http::string_body>& result() const;

  //################################################################################################
  boost::beast::http::response<boost::beast::http::string_body>& mutableResult();

  //################################################################################################
  void fail(boost::system::error_code ec, const std::string& whatFailed);

  //################################################################################################
  void setCompleted();

  //################################################################################################
  bool completed() const;

private:
  struct Private;
  friend struct Private;
  Private* d;
};

}

#endif
