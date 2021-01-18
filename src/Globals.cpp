#include "tp_http/Globals.h"

#include "tp_utils/DebugUtils.h"
#include "tp_utils/FileUtils.h"

#include "json.hpp"

#include <boost/asio/ssl/context.hpp>

#include <sstream>
#include <iomanip>

#ifdef TP_WIN32
#include <wincrypt.h>
#include <tchar.h>
#endif

namespace tp_http
{

//##################################################################################################
std::vector<std::string> protocols()
{
  return {"HTTP", "HTTPS"};
}

//##################################################################################################
std::string protocolToString(Protocol protocol)
{
  switch(protocol)
  {
  case Protocol::HTTP: return "HTTP";
  case Protocol::HTTPS: return "HTTPS";
  }
   return "HTTP";
}

//##################################################################################################
Protocol protocolFromString(const std::string& protocol)
{
  if(protocol == "HTTP")  return Protocol::HTTP;
  if(protocol == "HTTPS") return Protocol::HTTPS;
  return Protocol::HTTP;
}

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

#ifdef TP_WIN32
//##################################################################################################
namespace
{
// https://stackoverflow.com/questions/39772878/reliable-way-to-get-root-ca-certificates-on-windows
void add_windows_root_certs(boost::asio::ssl::context &sslCtx)
{
  HCERTSTORE hStore = CertOpenSystemStore(0, _T("ROOT"));
  if (hStore == nullptr) {
    return;
  }

  X509_STORE *store = X509_STORE_new();
  PCCERT_CONTEXT pContext = nullptr;
  while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
    X509 *x509 = d2i_X509(nullptr,
                          reinterpret_cast<const unsigned char **>(const_cast<const BYTE**>(&pContext->pbCertEncoded)),
                          long(pContext->cbCertEncoded));
    if(x509 != nullptr)
    {
      X509_STORE_add_cert(store, x509);
      X509_free(x509);
    }
  }

  CertFreeCertificateContext(pContext);
  CertCloseStore(hStore, 0);

  SSL_CTX_set_cert_store(sslCtx.native_handle(), store);
}
}
#endif

//##################################################################################################
void addSSLVerifyPaths(boost::asio::ssl::context& sslCtx)
{
#ifdef TP_HTTP_VERBOSE
  tpWarning() << "Adding certs";
#endif

#ifdef TP_WIN32
  add_windows_root_certs(sslCtx);
#else
  sslCtx.set_default_verify_paths();

  auto addPath = [&](const std::string& path)
  {
    if(tp_utils::exists(path))
      sslCtx.add_verify_path(path);
  };

  auto addFile = [&](const std::string& path)
  {
    if(tp_utils::exists(path))
      sslCtx.load_verify_file(path);
  };

  addPath("/etc/ssl/certs");
  addPath("/system/etc/security/cacerts");
  addPath("/usr/local/share/certs");
  addPath("/etc/pki/tls/certs");
  addPath("/etc/openssl/certs");
  addPath("/var/ssl/certs");

  addFile("/etc/ssl/certs/ca-certificates.crt");
  addFile("/etc/pki/tls/certs/ca-bundle.crt");
  addFile("/etc/ssl/ca-bundle.pem");
  addFile("/etc/pki/tls/cacert.pem");
  addFile("/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem");
  addFile("/etc/ssl/cert.pem");

#endif
}

}
