#include "tp_http/Request.h"

#include "tp_utils/DebugUtils.h"

namespace tp_http
{

//##################################################################################################
struct Request::Private
{
  const std::function<void(const Request&)> completionHandler;

  Protocol protocol{Protocol::HTTPS};
  std::string host;
  uint16_t port{0};
  boost::beast::http::verb verb{boost::beast::http::verb::get};
  std::string endpoint;
  std::unordered_map<std::string, std::string> headerData;
  std::unordered_map<std::string, std::string> formData;
  BodyEncodeMode bodyEncodeMode{BodyEncodeMode::URL};

  boost::beast::http::request<boost::beast::http::string_body> request;
  boost::beast::http::response<boost::beast::http::string_body> result;
  boost::system::error_code ec;
  std::string whatFailed;
  bool completed{false};

  //################################################################################################
  Private(const std::function<void(const Request&)>& completionHandler_):
    completionHandler(completionHandler_)
  {

  }
};

//##################################################################################################
Request::Request(const std::function<void(const Request&)>& completionHandler):
  d(new Private(completionHandler))
{

}

//##################################################################################################
Request::~Request()
{
  d->completionHandler(*this);
  delete d;
}

//##################################################################################################
void Request::setProtocol(Protocol protocol)
{
  d->protocol = protocol;
}

//##################################################################################################
Protocol Request::protocol() const
{
  return d->protocol;
}

//##################################################################################################
void Request::setHost(const std::string& host)
{
  d->host = host;
}

//##################################################################################################
const std::string& Request::host() const
{
  return d->host;
}

//##################################################################################################
void Request::setPort(uint16_t port)
{
  d->port = port;
}

//##################################################################################################
uint16_t Request::port() const
{
  return d->port?d->port:((d->protocol==Protocol::HTTP)?80:443);
}

//##################################################################################################
void Request::setVerb(boost::beast::http::verb verb)
{
  d->verb = verb;
}

//##################################################################################################
boost::beast::http::verb Request::verb() const
{
  return d->verb;
}

//##################################################################################################
void Request::setEndpoint(const std::string& endpoint)
{
  d->endpoint = endpoint;
}

//##################################################################################################
const std::string& Request::endpoint() const
{
  return d->endpoint;
}

//##################################################################################################
void Request::addHeaderData(const std::string& key, const std::string& value)
{
  d->headerData[key] = value;
}

//##################################################################################################
const std::unordered_map<std::string, std::string>& Request::headerData() const
{
  return d->headerData;
}

//##################################################################################################
void Request::addFormData(const std::string& key, const std::string& value)
{
  d->formData[key] = value;
}

//##################################################################################################
const std::unordered_map<std::string, std::string>& Request::formData() const
{
  return d->formData;
}

//##################################################################################################
void Request::setBodyEncodeMode(BodyEncodeMode bodyEncodeMode)
{
  d->bodyEncodeMode = bodyEncodeMode;
}

//##################################################################################################
BodyEncodeMode Request::bodyEncodeMode() const
{
  return d->bodyEncodeMode;
}

//##################################################################################################
void Request::generateRequest()
{
  //http version: 10->1.0, 11->1.1
  d->request.version(11);

  d->request.method(d->verb);
  d->request.target(d->endpoint);

  for(const auto& pair : d->headerData)
    d->request.set(pair.first, pair.second);

  std::string encodedBody;
  if(d->bodyEncodeMode == BodyEncodeMode::JSON)
  {
    d->request.set(boost::beast::http::field::content_type, "application/json");
    encodedBody = jsonEncodedForm(d->formData);

    d->request.target(d->endpoint + "?" + urlEncodedForm(d->formData));
  }
  else if(d->bodyEncodeMode == BodyEncodeMode::URL)
  {
    d->request.set(boost::beast::http::field::content_type, "application/x-www-form-urlencoded");
    encodedBody = urlEncodedForm(d->formData);
  }

  else if(d->bodyEncodeMode == BodyEncodeMode::MultiPart)
  {
    std::string boundary = "--d2cef45b-1cf5-42fb-875e-395bcd81293f";
    d->request.set(boost::beast::http::field::content_type, "multipart/form-data; boundary=\"" + boundary + "\"");
    encodedBody = multipartEncodedForm(d->formData, boundary);
  }

  d->request.set(boost::beast::http::field::content_length,  encodedBody.size());
  d->request.body() = encodedBody;
}

//##################################################################################################
const boost::beast::http::request<boost::beast::http::string_body>& Request::request() const
{
  return d->request;
}

//##################################################################################################
const boost::beast::http::response<boost::beast::http::string_body>& Request::result() const
{
  return d->result;
}

//##################################################################################################
boost::beast::http::response<boost::beast::http::string_body>& Request::mutableResult()
{
  return d->result;
}

//##################################################################################################
void Request::fail(boost::system::error_code ec, const std::string& whatFailed)
{
  d->ec = ec;
  d->whatFailed = whatFailed;
  tpDebug() << "Request::fail " << whatFailed;
}

//##################################################################################################
void Request::setCompleted()
{
  d->completed = true;
}

//##################################################################################################
bool Request::completed() const
{
  return d->completed;
}

}
