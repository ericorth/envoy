#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

namespace Envoy::Network {

class UpstreamUdpSocketFilter : public UpstreamDatagramHostSessionWriteFilter {
public:
  UdpSocketFilter(HostConstSharedPtr host);
  ~UpstreamUdpSocketFilter() final = default;

  // UpstreamDatagramHostSessionWriteFilter
  UdpSessionWriteFilterStatus onWrite(UdpRecvData& data) final;
  void initializeWriteFilterCallbacks(
      UpstreamDatagramHostSessionWriteFilterCallbacks* filter_callbacks) final;

private:
  HostConstSharedPtr host_;
  UpstreamDatagramHostSessionWriteFilterCallbacks* filter_callbacks_;
  SocketPtr socket_;
};

class UpstreamUdpSocketFilterConfigFactory :
    public Server::Configuration::NamedUpstreamDatagramHostSessionFilterFactory {
public:
  // NamedUpstreamDatagramHostSessionFilterFactory
  absl::StatusOr<UpstreamDatagramHostSessionFilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::UpstreamFactoryContext& context) final;
  ProtobufTypes::MessagePtr createEmptyConfigProto() final;
  std::string name() const final;
};

}  // namespace Envoy::Network
