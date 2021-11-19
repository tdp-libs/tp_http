#ifndef tp_http_Request_h
#define tp_http_Request_h

#include "tp_http/Globals.h"

#include <boost/beast/http.hpp>

#include <functional>

namespace tp_http
{

//##################################################################################################
//! New up one of these for each request.
class Request
{
  TP_NONCOPYABLE(Request);
public:
  //################################################################################################
  Request(const std::function<void(const Request&)>& completionHandler);

  //################################################################################################
  ~Request();

  //################################################################################################
  //! Select between HTTP and HTTPS.
  void setProtocol(Protocol protocol);

  //################################################################################################
  Protocol protocol() const;

  //################################################################################################
  //! Set the host name or ip.
  void setHost(const std::string& host);

  //################################################################################################
  const std::string& host() const;

  //################################################################################################
  //! Sets the port number to use, if this is not called 80 or 443 will be used.
  void setPort(uint16_t port);

  //################################################################################################
  //! Returns the port number.
  /*!
  Returns the port number, this will be inferred from the protocol if it has not been set expicitly.
  \return The port number.
  */
  uint16_t port() const;

  //################################################################################################
  //! Type of request. GET, POST, PUT, etc...
  void setVerb(boost::beast::http::verb verb);

  //################################################################################################
  //! GET, POST, PUT, etc...
  boost::beast::http::verb verb() const;

  //################################################################################################
  //! Sets the path part of the URL.
  void setEndpoint(const std::string& endpoint);

  //################################################################################################
  const std::string& endpoint() const;

  //################################################################################################
  //! Add raw header strings.
  void addHeaderData(const std::string& key, const std::string& value);

  //################################################################################################
  const std::unordered_map<std::string, std::string>& headerData() const;

  //################################################################################################
  //! Add data to be encoded in the document body.
  /*!
  This will be encoded in the body of the HTTP request based on the bodyEncodeMode. If the
  BodyEncodeMode::Raw is set the formPostData will be ignored and rawBodyData will be used instead.

  \param key of the value to add.
  \param value
  */
  void addFormPostData(const std::string& key, const PostData& value);

  //################################################################################################
  const std::unordered_map<std::string, PostData>& formPostData() const;

  //################################################################################################
  //! Add form data to be encoded on the end of the URL.
  void addFormGetData(const std::string& key, const std::string& value);

  //################################################################################################
  const std::unordered_map<std::string, std::string>& formGetData() const;

  //################################################################################################
  //! Sets the raw body data, only relevant if BodyEncodeMode::Raw is used.
  void setRawBodyData(const std::string& rawBodyData, const std::string& contentType);

  //################################################################################################
  const std::string& rawBodyData() const;

  //################################################################################################
  const std::string& contentType() const;

  //################################################################################################
  //! Controls how the body data is encoded.
  void setBodyEncodeMode(BodyEncodeMode bodyEncodeMode);

  //################################################################################################
  BodyEncodeMode bodyEncodeMode() const;

  //################################################################################################
  //! Call this once to populate the request structure.
  void generateRequest();

  //################################################################################################
  //! The request to send to boost beast.

#if BOOST_VERSION >= 107000
  const boost::beast::http::request<boost::beast::http::string_body>& request() const;
#else
  boost::beast::http::request<boost::beast::http::string_body>& request() const;
#endif

  //################################################################################################
  //! The response received from boost beast.
  const boost::beast::http::response<boost::beast::http::string_body>& result() const;

  //################################################################################################
  boost::beast::http::response<boost::beast::http::string_body>& mutableResult();

  //################################################################################################
  boost::beast::http::response_parser<boost::beast::http::string_body>& mutableParser();

  //################################################################################################
  void fail(const boost::system::error_code& ec, const std::string& whatFailed);

  //################################################################################################
  void setCompleted();

  //################################################################################################
  //! Set to true once the request has completed.
  bool completed() const;

  //################################################################################################
  void setAddedToClient();

  //################################################################################################
  bool addedToClient() const;

  //################################################################################################
  const std::string& whatFailed() const;

private:
  struct Private;
  friend struct Private;
  Private* d;
};

}

#endif
