#pragma once

#include <list>
#include <memory>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"
#include "source/common/common/linked_object.h"

namespace Envoy::Upstream {

class DatagramHostImpl : public DatagramHost,
                         public Network::UpstreamDatagramHostFilterManager {
public:
  DatagramHostImpl(HostConstSharedPtr host, Callbacks* callbacks);
  ~DatagramHostImpl() override = default;

  // DatagramHost
  void write(Network::UdpRecvData& data) override;

  // Network::UpstreamDatagramHostFilterManager
  const HostConstSharedPtr& host() const override { return host_; }
  void addReadFilter(Network::UpstreamDatagramHostReadFilterSharedPtr filter) override;
  void addWriteFilter(Network::UpstreamDatagramHostWriteFilterSharedPtr filter) override;
  void addFilter(Network::UpstreamDatagramHostFilterSharedPtr filter) override;

private:
  class ActiveReadFilter : public Network::UpstreamDatagramHostReadFilterCallbacks,
                           public LinkedObject<ActiveReadFilter> {
    public:
    ActiveReadFilter(Network::UpstreamDatagramHostReadFilterSharedPtr filter,
                     DatagramHostImpl* parent);
    ~ActiveReadFilter() override = default;

    Network::UpstreamDatagramHostReadFilter& filter() { return *filter_; }

    // Network::UpstreamDatagramHostReadFilterCallbacks
    void injectDatagramToFilterChain(Network::UdpRecvData& data) override;

    private:
    Network::UpstreamDatagramHostReadFilterSharedPtr filter_;
    DatagramHostImpl* parent_;
  };
  using ActiveReadFilterPtr = std::unique_ptr<ActiveReadFilter>;

  class ActiveWriteFilter : public Network::UpstreamDatagramHostWriteFilterCallbacks,
                            public LinkedObject<ActiveWriteFilter> {
    public:
    ActiveWriteFilter(Network::UpstreamDatagramHostWriteFilterSharedPtr filter,
                      DatagramHostImpl* parent);
    ~ActiveWriteFilter() override = default;

    Network::UpstreamDatagramHostWriteFilter& filter() { return *filter_; }

    // Network::UpstreamDatagramHostWriteFilterCallbacks
    void injectDatagramToFilterChain(Network::UdpRecvData& data) override;
    void readData(Network::UdpRecvData& data) override;

    private:
    Network::UpstreamDatagramHostWriteFilterSharedPtr filter_;
    DatagramHostImpl* parent_;
  };
  using ActiveWriteFilterPtr = std::unique_ptr<ActiveWriteFilter>;

  void injectDatagramToReadFilterChain(std::list<ActiveReadFilterPtr>::iterator iter,
                                       Network::UdpRecvData& data);
  void injectDatagramToWriteFilterChain(std::list<ActiveWriteFilterPtr>::iterator iter,
                                        Network::UdpRecvData& data);

  HostConstSharedPtr host_;
  Callbacks* const callbacks_;
  std::list<ActiveReadFilterPtr> read_filters_;
  std::list<ActiveWriteFilterPtr> write_filters_;
};

}  // namespace Envoy::Upstream
