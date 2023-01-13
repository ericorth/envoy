#include "source/extensions/upstreams/http/udp/upstream.h"

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/http/metadata_interface.h"
#include "quiche/common/masque/connect_udp_datagram_payload.h"
#include "quiche/quic/core/http/capsule.h"
#include "source/common/http/status.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

Upstream::Upstream() {}

Upstream::~Upstream() = default;

void Upstream::encodeData(Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    //!! Close the UDP socket (or release this upstream's use of it if we're pooling the sockets).
  }

  external_quiche_quic::CapsuleParser capsule_parser(this);
  for (const RawSlice& slice: data.GetRawSlices()) {
    capsule_parser.InjestCapsuleFragment(absl::string_view(slice.mem, slice.len));
  }
  capsule_parser.ErrorIfThereIsRemainingBufferedData();
}

bool Upstream::OnCapsule(quic::Capsule& capsule) {
  if (capsule.capsule_type() != quic::CapsuleType::DATAGRAM) {
    // Per RFC9297, Section 3.2, unknown Capsules with unknown Capsule Type must be silently dropped
    return true;
  }

  std::unique_ptr<quiche::ConnectUdpDatagramPayload> parsed =
      quiche::ConnectUdpDatagramPayload::Parse(capsule.datagram_capsule().http_datagram_payload);
  if (!parsed) {
    // Malformed DATAGRAM payload.
    return false;
  }

  if (parsed->GetType() != quiche::ConnectUdpDatagramPayload::Type::kUdpPacket) {
    // Per RFC9298, Section 5, unknown Datagram Payloads are silently dropped.
    return true;
  }
  absl::string_view udp_packet = parsed->GetUdpProxyingPayload();

  //!! Write to a socket. Maybe use a UdpPacketWriter, maybe constructed and passed by
  //!! the connection pool. Maybe more closely follow the example of UdpProxyFilter and more
  //!! directly deal with UDP sockets.  Or maybe, we'll hit enough complexity here to be worth
  //!! keeping an underlying UdpProxyFilter and reusing its work on writing UDP to Envoy clusters.
}

void Upstream::OnCapsuleParseFailure(const std::string& error_message) {
  //!! Malformed capsule input. Close the stream with a malformed error?
}

}  // namespace Envoy::Extensions::Upstreams::Http::Udp
