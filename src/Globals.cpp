#include "tp_http/Globals.h"

#include "json.hpp"

#include <sstream>
#include <iomanip>

//##################################################################################################
namespace tp_http
{

//##################################################################################################
std::string urlEncode(const std::string& value)
{
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for(const auto c : value)
  {
    auto isalnum = [](char c)
    {
      if(c>='a' && c<='z')
        return true;

      if(c>='A' && c<='Z')
        return true;

      if(c>='0' && c<='9')
        return true;

      return false;
    };

    if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << std::uppercase;
    escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
    escaped << std::nouppercase;
  }

  return escaped.str();
}

//##################################################################################################
std::string jsonEncodedForm(const std::unordered_map<std::string, std::string>& formData)
{
  nlohmann::json j;

  for(const auto& i : formData)
    j[i.first] = i.second;

  return j.dump();
}

//##################################################################################################
std::string urlEncodedForm(const std::unordered_map<std::string, std::string>& formData)
{
  size_t size{0};
  for(const auto& pair : formData)
    size += 1 + pair.first.size() + pair.second.size();

  std::string result;
  result.reserve(size*2);
  for(const auto& pair : formData)
  {
    if(!result.empty())
      result += '&';

    result += urlEncode(pair.first) + '=' + urlEncode(pair.second);
  }

  return result;
}

//##################################################################################################
std::string multipartEncodedForm(const std::unordered_map<std::string, std::string>& formData, const std::string& boundary)
{
  size_t size{0};
  for(const auto& pair : formData)
    size += 1 + pair.first.size() + pair.second.size();

  //Allocate extra space for the boundaries and associated header overhead.
  size += (formData.size()+1) * (100 + boundary.size());

  std::string result;
  result.reserve(size*2);
  for(const auto& pair : formData)
  {
    result += "--" + boundary + "\r\n";
    result += "Content-Disposition: form-data; name=\"" + pair.first+ "\"\r\n\r\n";
    result += pair.second + "\r\n";
  }

  if(!result.empty())
    result += "--" + boundary + "--\r\n";

  return (result);
}

}
