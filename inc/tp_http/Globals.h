#ifndef tp_http_Globals_h
#define tp_http_Globals_h

#include "tp_utils/StringID.h"

#if defined(TP_HTTP_LIBRARY)
#  define TP_HTTP_SHARED_EXPORT TP_EXPORT
#else
#  define TP_HTTP_SHARED_EXPORT TP_IMPORT
#endif

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
  JSON,
  URL,
  MultiPart
};

//##################################################################################################
std::string urlEncode(const std::string& value);

//##################################################################################################
std::string jsonEncodedForm(const std::unordered_map<std::string, std::string>& formData);

//##################################################################################################
std::string urlEncodedForm(const std::unordered_map<std::string, std::string>& formData);

//##################################################################################################
std::string multipartEncodedForm(const std::unordered_map<std::string, std::string>& formData, const std::string& boundary);

}

#endif
