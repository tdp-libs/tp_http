#ifndef tp_http_ResolverResults_h
#define tp_http_ResolverResults_h

#include "tp_http/Globals.h" // IWYU pragma: keep

#include <boost/asio/ip/tcp.hpp>

namespace tp_http
{

//##################################################################################################
struct ResolverResults
{
  boost::asio::ip::tcp::resolver::results_type resolverResults;
};

}

#endif
