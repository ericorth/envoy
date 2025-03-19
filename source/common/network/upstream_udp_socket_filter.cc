#include "source/common/network/upstream_udp_socket_filter.h"

#include <memory>
#include <utility>

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"
#include "envoy/network/socket_interface.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"
#include "source/common/common/assert.h"
#include "source/common/network/socket_impl.h"

namespace Envoy::Network {

UpstreamUdpSocketFilter::UpstreamUdpSocketFilter(Upstream::HostConstSharedPtr host,
                                                 bool allow_bind,
                                                 Socket::OptionsSharedPtr additional_socket_options,
                                                 uint64_t max_datagram_size,
                                                 TimeSource* time_source, bool prefer_gro,
                                                 Event::Dispatcher* dispatcher)
    : host_(std::move(host)), allow_bind_(allow_bind),
      additional_socket_options_(std::move(additional_socket_options)),
      max_datagram_size_(max_datagram_size), time_source_(time_source),
      prefer_gro_(prefer_gro), dispatcher_(dispatcher) {
  ASSERT(host_);
}

FilterStatus UpstreamUdpSocketFilter::onWrite(UdpRecvData& data) {
  ASSERT(filter_callbacks_);

  absl::Status init_status = maybeInitializeSocket();
  if (!init_status.ok()) {
    //!! handle failure?
    return FilterStatus::StopIteration;
  }

  const uint64_t tx_buffer_length = data.buffer_->length();
  //!! log?

  Api::IoCallUint64Result write_result = Network::Utility::writeToSocket(
      socket_->ioHandle(), *data.buffer_, /*local_ip=*/nullptr, *host_->address());

  if (!write_result.ok()) {
    host_->cluster()->cluster_stats_.sess_tx_errors_.inc();
  } else {
    host_->cluster()->cluster_stats_.sess_tx_datagrams_.inc();
    host_->cluster()->cluster_info_->trafficStats()->upstream_cx_tx_bytes_total_.add(
        tx_buffer_length);
  }

  // This is a terminal filter, sending `data` to the network, so no further filters are expected or
  // supported.
  return FilterStatus::StopIteration;
}

void UpstreamUdpSocketFilter::initializeWriteFilterCallbacks(
    UpstreamDatagramHostSessionWriteFilterCallbacks* filter_callbacks) {
  ASSERT(filter_callbacks);
  ASSERT(!filter_callbacks_);
  filter_callbacks_ = filter_callbacks;
}

absl::Status UpstreamUdpSocketFilter::maybeInitializeSocket() {
  ASSERT(host_);

  if (socket_) {
    ASSERT(socket_->isOpen());
    return absl::OkStatus();
  }

  SocketPtr socket = std::make_unique<SocketImpl>(Socket::Type::Datagram,
                                                  /*address_for_io_handle=*/host_->address(),
                                                  /*remote_address=*/nullptr,
                                                  SocketCreationOptions{});
  RELEASE_ASSERT(socket->isOpen(), "Socket creation fail");

  if (allow_bind_) {
    Upstream::UpstreamLocalAddress local_address =
        host_->cluster().getUpstreamLocalAddressSelector()->getUpstreamLocalAddress(
            host_->address(), /*socket_options=*/nullptr);
    if (additional_socket_options_) {
      Socket::appendOptions(local_address.socket_options_,
                            additional_socket_options_);
    }
    bool apply_options_result =
        Socket::applyOptions(local_address.socket_options_, *socket,
                             envoy::config::core::v3::SocketOption::STATE_PREBIND));
    if (!apply_options_result) {
      //!! Log?
      //!! Pass failure back through reverse filter chain?
      return absl::UnavailableError("Failed to apply socket options");
    }
    if (local_address.address_) {
      Api::SysCallIntResult bind_result = socket->bind(local_address.address_);
      if (SOCKET_FAILURE(bind_result.return_value_)) {
        //!! Log?
        //!! Pass failure back through reverse filter chain?
        return absl::UnavailableError("Failed to bind socket");
      }
    }
  } else if (additional_socket_options_) {
    bool apply_options_result =
        Socket::applyOptions(additional_socket_options_, *socket,
                             envoy::config::core::v3::SocketOption::STATE_PREBIND));
    if (!apply_options_result) {
      //!! Log?
      //!! Pass failure back through reverse filter chain?
      return absl::UnavailableError("Failed to apply socket options");
    }
  }

  socket_->ioHandle().initializeFileEvent(
      *dispatcher_,
      [this](uint32_t) {
        onSocketReadReady();
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  Api::SysCallIntResult connect_result = socket->connect(host_->address());
  if (SOCKET_FAILURE(rc.return_value_)) {
    return absl::UnavailableError("Upstream socket connect failure.");
  }

  socket_ = std::move(socket);
  return absl::OkStatus();
}

void UpstreamUdpSocketFilter::onSocketReadReady() {
  //!! resetIdleTimer(); ??

  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Utility::readPacketsFromSocket(
      socket_->ioHandle(), *socket_->connectionInfoProvider().localAddress(), *this,
      *time_source_, prefer_gro_, /*allow_mmsg=*/true, packets_dropped);

  if (result == nullptr) {
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }

  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    cluster_->cluster_stats_.sess_rx_errors_.inc();
  }

  //!! Flush out buffered data at the end of IO event.
  //!! Or maybe handle it simply via reverse filter chain?
}

REGISTER_FACTORY(UpstreamUdpSocketFilterConfigFactory,
                 NamedUpstreamDatagramHostFilterFactory);

}  // namespace Envoy::Network
