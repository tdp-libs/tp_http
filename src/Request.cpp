#include "tp_http/Request.h"

#include "tp_utils/DebugUtils.h"
#include "tp_utils/RefCount.h"
#ifdef TP_HTTP_DEBUG
#include "tp_utils/StackTrace.h"
#endif

#include <iostream>
#include <sstream>
namespace tp_http
{

//##################################################################################################
struct Request::Private
{
  TP_REF_COUNT_OBJECTS("tp_http::Request::Private");

  std::weak_ptr<int> alive;
  const std::function<void(float, size_t, size_t)> progressCallback;
  const std::function<void(const Request&)> completionHandler;

  Protocol protocol{Protocol::HTTPS};
  std::string host;
  uint16_t port{0};
  boost::beast::http::verb verb{boost::beast::http::verb::get};
  std::string endpoint;
  std::unordered_map<std::string, std::string> headerData;
  std::unordered_multimap<std::string, PostData   > formPostData;
  std::unordered_multimap<std::string, std::string> formGetData;
  std::string rawBodyData;
  std::string contentType;
  BodyEncodeMode bodyEncodeMode{BodyEncodeMode::URL};

  boost::beast::http::request<boost::beast::http::string_body> request;
  boost::beast::http::response_parser<boost::beast::http::string_body> parser;

  std::string whatFailed;
  bool completed{false};
  bool addedToClient{false};

  //################################################################################################
  Private(const std::weak_ptr<int>& alive_,
          const std::function<void(float, size_t, size_t)>& progressCallback_,
          const std::function<void(const Request&)>& completionHandler_):
    alive(alive_),
    progressCallback(progressCallback_),
    completionHandler(completionHandler_)
  {
    parser.body_limit(4096ull * 1024 * 1024);
  }
};

//##################################################################################################
Request::Request(const std::weak_ptr<int>& alive,
                 const std::function<void(const Request&)>& completionHandler):
  d(new Private(alive, std::function<void(float, size_t, size_t)>(), completionHandler))
{

}

//##################################################################################################
Request::Request(const std::weak_ptr<int>& alive,
                 const std::function<void(float, size_t, size_t)>& progressCallback,
                 const std::function<void(const Request&)>& completionHandler):
  d(new Private(alive, progressCallback, completionHandler))
{

}

//##################################################################################################
Request::~Request()
{
#ifdef TP_HTTP_VERBOSE
  tpWarning() << "Request completed ("
                 "protocol: " << protocolToString(d->protocol) <<
                 ", host: " << d->host <<
                 ", port: " << d->port <<
                 ", verb: " << boost::beast::http::to_string(d->verb) <<
                 ", endpoint: " << d->endpoint <<
                 ", body size: " << d->parser.get().body().size() <<
                 ", error: " << d->whatFailed <<
                 ", completed: " << d->completed <<
                 ", result: " << int(d->parser.get().result()) << ")";
#endif

  if(!d->alive.expired())
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
void Request::addFormPostData(const std::string& key, const PostData& value)
{
  d->formPostData.insert({key, value});
}

//##################################################################################################
const std::unordered_multimap<std::string, PostData>& Request::formPostData() const
{
  return d->formPostData;
}

//##################################################################################################
void Request::addFormGetData(const std::string& key, const std::string& value)
{
  d->formGetData.insert({key, value});
}

//##################################################################################################
const std::unordered_multimap<std::string, std::string>& Request::formGetData() const
{
  return d->formGetData;
}

//##################################################################################################
void Request::setRawBodyData(const std::string& rawBodyData, const std::string& contentType)
{
  d->rawBodyData = rawBodyData;
  d->contentType = contentType;
}

//##################################################################################################
const std::string& Request::rawBodyData() const
{
  return d->rawBodyData;
}

//##################################################################################################
const std::string& Request::contentType() const
{
  return d->contentType;
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
    encodedBody = jsonEncodedForm(d->formPostData);
  }
  else if(d->bodyEncodeMode == BodyEncodeMode::URL)
  {
    d->request.set(boost::beast::http::field::content_type, "application/x-www-form-urlencoded");
    encodedBody = urlEncodedForm(d->formPostData);
  }

  else if(d->bodyEncodeMode == BodyEncodeMode::MultiPart)
  {
    std::string boundary = "--d2cef45b-1cf5-42fb-875e-395bcd81293f";
    d->request.set(boost::beast::http::field::content_type, "multipart/form-data; boundary=\"" + boundary + "\"");
    encodedBody = multipartEncodedForm(d->formPostData, boundary);
  }

  else if(d->bodyEncodeMode == BodyEncodeMode::Raw)
  {
    d->request.set(boost::beast::http::field::content_type, d->contentType);
    encodedBody = d->rawBodyData;
  }

  else if(d->bodyEncodeMode == BodyEncodeMode::Empty)
  {
    //d->request.set(boost::beast::http::field::content_type, d->contentType);
    encodedBody = d->rawBodyData;
  }

  if(!d->formGetData.empty())
    d->request.target(d->endpoint + "?" + urlEncodedForm(d->formGetData));

  if(d->bodyEncodeMode != BodyEncodeMode::Empty)
  {
    d->request.set(boost::beast::http::field::content_length,  std::to_string(encodedBody.size()));
    d->request.body() = encodedBody;
  }
}

//##################################################################################################
std::string Request::toString()
{
  std::stringstream ss;
  ss<<"\n--------------------------------- Request ---------------------------------"<<
  "\n    Protocol: "<< (protocol() == tp_http::Protocol::HTTP ? "HTTP" : "HTTPS")<<
  "\n    Verb: "<< verb()<<
  "\n    Host: "<<host()<<
  "\n    Endpoint: "<<endpoint()<<
  "\n    Port: "<< port();

  if(headerData().size())
    ss<<"\n    -- Header data:";
  for(auto &data : headerData())
    ss<<"\n        Key: '"<<data.first<<"' Data: '"<<data.second<<"'";

  if(formGetData().size())
    ss<<"\n    -- Get query parameters:";
  for(auto &data : formGetData())
    ss<<"\n        Key: '"<<data.first<<"' Data: '"<<data.second<<"'";
  
  if(formPostData().size())
    ss<<"\n    -- Post query parameters:";
  for(auto &data : formPostData())
    ss<<"\n        Key: '"<<data.first<<"' Data: '"<<data.second.data<<"'";

  ss<<
  "\n    ContentType: "<<contentType()<<
  "\n    Body: "<<rawBodyData()<<
  "\n---------------------------------------------------------------------------\n";
  return ss.str();
}

//##################################################################################################
#if BOOST_VERSION >= 107000
const boost::beast::http::request<boost::beast::http::string_body>& Request::request() const
#else
boost::beast::http::request<boost::beast::http::string_body>& Request::request() const
#endif
{
  return d->request;
}

//##################################################################################################
boost::beast::http::request<boost::beast::http::string_body>& Request::mutableRequest() const
{
  return d->request;
}

//##################################################################################################
const boost::beast::http::response<boost::beast::http::string_body>& Request::result() const
{
  return d->parser.get();
}

//##################################################################################################
boost::beast::http::response<boost::beast::http::string_body>& Request::mutableResult()
{
  return d->parser.get();
}

//##################################################################################################
boost::beast::http::response_parser<boost::beast::http::string_body>& Request::mutableParser()
{
  return d->parser;
}

//##################################################################################################
void Request::fail(const boost::system::error_code& ec, const std::string& whatFailed)
{
  d->whatFailed = whatFailed + " ec: " + ec.message() + " code: " + std::to_string(ec.value());

#ifdef TP_HTTP_DEBUG
  // WARNING: If you get a crash here it may be bost headers not mathing the compiled boost_system
  // make sure you don't have multiple versions of Boost installed.
  tpWarning() << "Request::fail " << whatFailed << " ec: " << ec.message();

  tp_utils::printStackTrace();
#endif
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

//##################################################################################################
void Request::setAddedToClient()
{
  d->addedToClient = true;
}

//##################################################################################################
bool Request::addedToClient() const
{
  return d->addedToClient;
}

//##################################################################################################
const std::string& Request::whatFailed() const
{
  return d->whatFailed;
}

//##################################################################################################
void Request::setProgress(float fraction, size_t uploadSize, size_t downloadSize)
{
  if(d->progressCallback && !d->alive.expired())
    d->progressCallback(fraction, uploadSize, downloadSize);
}

}
