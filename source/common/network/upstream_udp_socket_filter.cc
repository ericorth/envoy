#include "source/common/network/upstream_udp_socket_filter.h"

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

namespace Envoy::Network {

UpstreamUdpSocketFilter::UpstreamUdpSocketFilter(
    UpstreamDatagramHostSessionWriteFilterCallbacks& filter_callbacks)
  : filter_callbacks_(&filter_callbacks) {}

UdpSessionWriteFilterStatus UpstreamUdpSocketFilter::onWrite(UdpRecvData& data) {

}

REGISTER_FACTORY(UpstreamUdpSocketFilterConfigFactory,
                 NamedUpstreamDatagramHostSessionFilterFactory);

}  // namespace Envoy::Network
