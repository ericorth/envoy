#include "source/extensions/upstreams/http/udp/connection_pool.h"

#include "envoy/router/router.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/thread_local_cluster.h"

namespace Envoy::Extensions::Upstreams::Http::Udp {

ConnectionPool::ConnectionPool(Upstream::ThreadLocalCluster& thread_local_cluster) {
  //!! Get some more persistent object out of the TLC for making UDP sockets
  //!! to the upstream.  HTTP and TCP connection pools have specific
  //!! HTTP and TCP *ConnPoolData objects that the TCL knows how to give.
  //!! Do we need to do all that, or can we just get something simpler
  //!! since there's a lot less complexity needed to open a UDP socket.
  //!! We essentially just need something to disperse a socket, or maybe
  //!! just host/port every time it's called.
  //!!
  //!! Looks like we could grab the LoadBalancer and just request a host
  //!! from that, but there appears to be inaccessible override logic to
  //!! potentially get a different host from the
  //!! ThreadLocalClusterManagerImpl::ClusterEntry that is backing the
  //!! ThreadLocalCluster. So I think we're stuck with creating some sort
  //!! of new UDP pool accessor in ThreadLocalCluster, but we can probably
  //!! keep it simple.
}

ConnectionPool::~ConnectionPool() = default;

}  // namespace Envoy::Extensions::Upstreams::Http::Udp
