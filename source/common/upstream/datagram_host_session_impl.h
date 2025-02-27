#pragma once

#include "envoy/network/filter.h"

namespace Envoy::Upstream {

class DatagramHostSessionImpl : public DatagramHostSession,
                                public Event::Network::UdpSessionFilterChainFactoryCallbacks {
public:
  DatagramHostSessionImpl(Callbacks* callbacks);
  override ~DatagramHostSessionImpl() override = default;

  // DatagramHostSession
  override void write(Network::UdpRecvData& data);

  // Event::Network::UdpSessionFilterChainFactoryCallbacks
  override void addReadFilter(Network::UdpSessionReadFilterSharedPtr filter);
  override void addWriteFilter(Network::UdpSessionWriteFilterSharedPtr filter);
  override void addFilter(Network::UdpSessionFilterSharedPtr filter);

private:
  Callbacks* const callbacks_;
  std::list<Network::UdpSessionReadFilterSharedPtr> read_filters_;
  std::list<Network::UdpSessionWriteFilterSharedPtr> write_filters_;
};

}  // namespace Envoy::Upstream
