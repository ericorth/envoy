#pragma once

#include "envoy/http/header_map.h"
#include "source/common/http/status.h"
#include "source/extensions/upstreams/http/udp/upstream.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

// Upstream handler that synthesizes response headers in order to implement the CONNECT-UDP protocol
// (RFC9298).
class ConnectUdpUpstream : public Upstream {
 public:
  ConnectUdpUpstream();
  ~ConnectUdpUpstream() override;

  // ConnectUdpUpstream is neither copyable nor movable.
  ConnectUdpUpstream(const ConnectUdpUpstream&) = delete;
  ConnectUdpUpstream& operator=(const ConnectUdpUpstream&) = delete;

  // Upstream
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                    bool end_stream) override;
};

}  // namespace Envoy::Extensions::Upstreams::Http::Udp
