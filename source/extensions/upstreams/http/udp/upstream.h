#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/router/router.h"
#include "quiche/quic/core/http/capsule.h"
#include "source/common/http/status.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

// Upstream handler that converts between HTTP Datagram Capsules received from the downstream to UDP
// sent to the upstream, and similarly converts UDP received from the upstream to HTTP Datagram
// Capsules sent back to the downstream.
class Upstream
    : public Router::GenericUpstream, public quic::CapsuleParser::Visitor {
 public:
  Upstream();
  ~Upstream() override;

  // Upstream is neither copyable nor movable.
  Upstream(const Upstream&) = delete;
  Upstream& operator=(const Upstream&) = delete;

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                    bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override;
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}

  // CapsuleParser::Visitor
  bool OnCapsule(quic::Capsule& capsule) override;
  void OnCapsuleParseFailure(const std::string& error_message) override;

 private:
  //!! Maybe Network::UdpPacketWriterPtr? Or maybe just keep the IO handle and do baser
  //!! socket operations on that?
};

}  // namespace Envoy::Extensions::Upstreams::Http::Udp {
