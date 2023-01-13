#pragma once

#include "envoy/router/router.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/thread_local_cluster.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

class ConnectionPool : public Router::GenericConnPool {
 public:
  ConnectionPool(Upstream::ThreadLocalCluster& thread_local_cluster);
  ~ConnectionPool() override;

  // ConnectionPool is neither copyable nor movable.
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  // Router::GenericConnPool
  void newStream(GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingStream() override;
  Upstream::HostDescriptionConstSharedPtr host() const override;
};

}  // namespace Envoy::Extensions::Upstreams::Http::Udp
