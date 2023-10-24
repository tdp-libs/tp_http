#ifndef tp_http_Globals_h
#define tp_http_Globals_h

#include "tp_utils/StringID.h"

#include <unordered_map>

#if defined(TP_HTTP_LIBRARY)
#  define TP_HTTP_EXPORT TP_EXPORT
#else
#  define TP_HTTP_EXPORT TP_IMPORT
#endif

namespace boost::asio::ssl
{
class context;
}

//##################################################################################################
namespace tp_http
{

//##################################################################################################
enum class Protocol
{
  HTTP,
  HTTPS
};

//##################################################################################################
std::vector<std::string> protocols();

//##################################################################################################
std::string protocolToString(Protocol protocol);

//##################################################################################################
Protocol protocolFromString(const std::string& protocol);

//##################################################################################################
enum class BodyEncodeMode
{
  JSON,      //!< The formPostData will be encoded as JSON and the content type set to "application/json".
  URL,       //!< The formPostData URL encoded and the content type set to "application/x-www-form-urlencoded".
  MultiPart, //!< The formPostData will be multi part form encoded and the content type set to "multipart/form-data; boundary=".
  Raw,       //!< The rawBodyData and contentType will be used.
  Empty
};

//##################################################################################################
struct PostData
{
  std::string data;
  std::string filename;

  PostData()=default;

  //################################################################################################
  PostData(const std::string& data_, const std::string& filename_=std::string()):
    data(data_),
    filename(filename_)
  {

  }
};

//##################################################################################################
std::string urlEncode(const std::string& value);

//##################################################################################################
std::string jsonEncodedForm(const std::list<std::pair<std::string, PostData>>& formData);

//##################################################################################################
std::string urlEncodedForm(const std::list<std::pair<std::string, PostData>>& formData);

//##################################################################################################
std::string urlEncodedForm(const std::unordered_multimap<std::string, std::string>& formData);

//##################################################################################################
std::string multipartEncodedForm(const std::list<std::pair<std::string, PostData>>& formData, const std::string& boundary);

//##################################################################################################
void addSSLVerifyPaths(boost::asio::ssl::context& sslCtx);

}

#endif
