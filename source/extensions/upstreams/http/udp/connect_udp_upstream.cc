#include "source/extensions/upstreams/http/udp/connect_udp_upstream.h"

#include "envoy/http/header_map.h"
#include "source/common/http/status.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

ConnectUdpUpstream::ConnectUdpUpstream() {}

ConnectUdpUpstream::~ConnectUdpUpstream() = default;

Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers, bool end_stream) {
  //!! Look to the example in TcpUpstream to create response headers. Note that this code will
  //!! probably have to have the silliness of following the CONNECT-UDP spec for an http/1.1
  //!! "101 Switching Protocols" response, which the codecs should support converting to an h2 or h3
  //!! Extended Connect response as necessary.

  return Upstream::encodeHeaders(headers, end_stream);
}

}  // namespace Envoy::Extensions::Upstreams::Http::Udp
