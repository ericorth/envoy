#include "source/common/upstream/datagram_host_impl.h"

#include <iterator>
#include <utility>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/upstream/upstream.h"
#include "source/common/common/assert.h"
#include "source/common/common/linked_object.h"

namespace Envoy::Upstream {

DatagramHostImpl::DatagramHostImpl(HostConstSharedPtr host, Callbacks* callbacks)
  : host_(std::move(host)), callbacks_(callbacks) {}

void DatagramHostImpl::write(Network::UdpRecvData& data) {
  //!! logging/stats

  injectDatagramToWriteFilterChain(write_filters_.begin(), data);
}

void DatagramHostImpl::addReadFilter(Network::UpstreamDatagramHostReadFilterSharedPtr filter) {
  LinkedList::moveIntoList(std::make_unique<ActiveReadFilter>(std::move(filter), this),
                               read_filters_);
}

void DatagramHostImpl::addWriteFilter(Network::UpstreamDatagramHostWriteFilterSharedPtr filter) {
  LinkedList::moveIntoListBack(std::make_unique<ActiveWriteFilter>(std::move(filter), this),
                               write_filters_);
}

void DatagramHostImpl::addFilter(Network::UpstreamDatagramHostFilterSharedPtr filter) {
  addReadFilter(filter);
  addWriteFilter(std::move(filter));
}

DatagramHostImpl::ActiveReadFilter::ActiveReadFilter(
    Network::UpstreamDatagramHostReadFilterSharedPtr filter, DatagramHostImpl* parent)
    : filter_(std::move(filter)), parent_(parent) {
  ASSERT(filter_);
  ASSERT(parent_);

  filter_->initializeReadFilterCallbacks(this);
}

void DatagramHostImpl::ActiveReadFilter::injectDatagramToFilterChain(Network::UdpRecvData& data) {
  ASSERT(inserted());
  parent_->injectDatagramToReadFilterChain(std::next(entry()), data);
}

DatagramHostImpl::ActiveWriteFilter::ActiveWriteFilter(
    Network::UpstreamDatagramHostWriteFilterSharedPtr filter, DatagramHostImpl* parent)
    : filter_(std::move(filter)), parent_(parent) {
  ASSERT(filter_);
  ASSERT(parent_);

  filter_->initializeWriteFilterCallbacks(this);
}

void DatagramHostImpl::ActiveWriteFilter::injectDatagramToFilterChain(Network::UdpRecvData& data) {
  ASSERT(inserted());
  parent_->injectDatagramToWriteFilterChain(std::next(entry()), data);
}

void DatagramHostImpl::ActiveWriteFilter::readData(Network::UdpRecvData& data) {
  parent_->injectDatagramToReadFilterChain(parent_->read_filters_.begin(), data);
}

void DatagramHostImpl::injectDatagramToReadFilterChain(
    std::list<ActiveReadFilterPtr>::iterator iter, Network::UdpRecvData& data) {
  for (; iter != read_filters_.end(); iter++) {
    Network::FilterStatus status = (*iter)->filter().onRead(data);
    if (status != Network::FilterStatus::Continue) {
      return;
    }
  }

  callbacks_->onDatagramRead(data);
}

void DatagramHostImpl::injectDatagramToWriteFilterChain(
    std::list<ActiveWriteFilterPtr>::iterator iter, Network::UdpRecvData& data) {
  for (; iter != write_filters_.end(); iter++) {
    Network::FilterStatus status = (*iter)->filter().onWrite(data);
    if (status != Network::FilterStatus::Continue) {
      return;
    }
  }
}

}  // namespace Envoy::Upstream
