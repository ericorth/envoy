#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"
#include "envoy/upstream/upstream.h"

namespace Envoy::Network {

class UpstreamUdpSocketFilter : public UpstreamDatagramHostWriteFilter,
                                public UdpPacketProcessor {
public:
  UpstreamUdpSocketFilter(Upstream::HostConstSharedPtr host,
                          bool allow_bind,
                          Socket::OptionsSharedPtr additional_socket_options,
                          uint64_t max_datagram_size,
                          TimeSource* time_source,
                          bool prefer_gro,
                          Event::Dispatcher* dispatcher);
  ~UpstreamUdpSocketFilter() final = default;

  // UpstreamDatagramHostWriteFilter
  FilterStatus onWrite(UdpRecvData& data) final;
  void initializeWriteFilterCallbacks(
      UpstreamDatagramHostWriteFilterCallbacks* filter_callbacks) final;

  // UdpPacketProcessor
  void processPacket(Address::InstanceConstSharedPtr local_address,
                     Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::OwnedImpl saved_cmsg) final;
  uint64_t maxDatagramSize() const override { return max_datagram_size_; }
  void onDatagramsDropped(uint32_t dropped) override {
    host_->cluster()->cluster_stats_.sess_rx_datagrams_dropped_.add(dropped);
  }
  size_t numPacketsExpectedPerEventLoop() const override {
    return MAX_NUM_PACKETS_PER_EVENT_LOOP;
  }
  const IoHandle::UdpSaveCmsgConfig& saveCmsgConfig() const override {
    static const IoHandle::UdpSaveCmsgConfig empty_config{};
    return empty_config;
  };

private:
  absl::Status maybeInitializeSocket();
  void onSocketReadReady();

  HostConstSharedPtr host_;
  const bool allow_bind_;
  const Socket::OptionsSharedPtr additional_socket_options_;
  const uint64_t max_datagram_size_;
  TimeSource* const time_source_;
  const bool prefer_gro_;
  Event::Dispatcher* const dispatcher_;

  UpstreamDatagramHostWriteFilterCallbacks* filter_callbacks_;
  SocketPtr socket_;
};

class UpstreamUdpSocketFilterConfigFactory :
    public Server::Configuration::NamedUpstreamDatagramHostFilterFactory {
public:
  // Server::Configuration::NamedUpstreamDatagramHostFilterFactory
  absl::StatusOr<UpstreamDatagramHostFilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::UpstreamFactoryContext& context) final;
  ProtobufTypes::MessagePtr createEmptyConfigProto() final;
  std::string name() const final;
};

}  // namespace Envoy::Network
