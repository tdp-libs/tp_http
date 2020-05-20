#ifndef tp_http_Globals_h
#define tp_http_Globals_h

#include "tp_utils/StringID.h"

#if defined(TP_HTTP_LIBRARY)
#  define TP_HTTP_SHARED_EXPORT TP_EXPORT
#else
#  define TP_HTTP_SHARED_EXPORT TP_IMPORT
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
enum class BodyEncodeMode
{
  JSON,      //!< The formPostData will be encoded as JSON and the content type set to "application/json".
  URL,       //!< The formPostData URL encoded and the content type set to "application/x-www-form-urlencoded".
  MultiPart, //!< The formPostData will be multi part form encoded and the content type set to "multipart/form-data; boundary=".
  Raw        //!< The rawBodyData and contentType will be used.
};

//##################################################################################################
std::string urlEncode(const std::string& value);

//##################################################################################################
std::string jsonEncodedForm(const std::unordered_map<std::string, std::string>& formData);

//##################################################################################################
std::string urlEncodedForm(const std::unordered_map<std::string, std::string>& formData);

//##################################################################################################
std::string multipartEncodedForm(const std::unordered_map<std::string, std::string>& formData, const std::string& boundary);

//##################################################################################################
void addSSLVerifyPaths(boost::asio::ssl::context& sslCtx);

}

#endif
