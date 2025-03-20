#include "source/common/upstream/datagram_host_impl.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/functional/bind_front.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "envoy/network/listener.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy::Upstream {
namespace {

using ::Envoy::Network::FilterStatus;
using ::Envoy::Network::MockUpstreamDatagramHostFilter;
using ::Envoy::Network::UpstreamDatagramHostWriteFilter;
using ::Envoy::Network::UpstreamDatagramHostWriteFilterCallbacks;
using ::Envoy::Network::UdpRecvData;
using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;
using ::testing::StrictMock;

class DatagramHostImplTest : public testing::Test,
                             public DatagramHost::Callbacks {
 public:
  ~DatagramHostImplTest() override = default;

  // DatagramHost::Callbacks
  void onDatagramRead(UdpRecvData& data) override {
    UdpRecvData copy{data.addresses_, std::make_unique<Buffer::OwnedImpl>(*data.buffer_),
                     data.receive_time_, data.tos_, {}};
    copy.saved_cmsg_.add(data.saved_cmsg_);
    received_datagrams_.push_back(std::move(copy));
  }

  std::vector<UdpRecvData> received_datagrams_;

  std::shared_ptr<MockClusterInfo> cluster_ = std::make_shared<NiceMock<MockClusterInfo>>();
  Event::SimulatedTimeSystem time_system_;
  HostSharedPtr host_ = makeTestHost(cluster_, "hostname", "udp://127.0.0.1:80", time_system_);
};


// Write filter that takes the write datagram and reflects back a copy with local and peer addresses
// reversed and an updated receive time. Used in place of a terminal filter that would normally
// perform the upstream network interaction, e.g. using a UDP socket.
class ReflectDatagramFilter : public UpstreamDatagramHostWriteFilter {
  public:
   ReflectDatagramFilter(TimeSource* time_source) : time_source_(time_source) {
     ASSERT(time_source_);
   }

   ~ReflectDatagramFilter() override = default;

   // UpstreamDatagramHostWriteFilter

  FilterStatus onWrite(UdpRecvData& data) override {
    ASSERT(callbacks_);

    UdpRecvData copy{{data.addresses_.peer_, data.addresses_.local_},
                      std::make_unique<Buffer::OwnedImpl>(*data.buffer_),
                      time_source_->monotonicTime(), data.tos_, {}};
    copy.saved_cmsg_.add(data.saved_cmsg_);
    callbacks_->readData(copy);

    return FilterStatus::StopIteration;
  }

  void initializeWriteFilterCallbacks(
      UpstreamDatagramHostWriteFilterCallbacks* filter_callbacks) override {
    ASSERT(!callbacks_);
    ASSERT(filter_callbacks);
    callbacks_ = filter_callbacks;
  }

  private:
    TimeSource* time_source_;
    UpstreamDatagramHostWriteFilterCallbacks* callbacks_ = nullptr;
};

// Read/write filter that on every call will stop the filter chain and resume only when requested by
// the test.
class AsyncTriggerFilter : public MockUpstreamDatagramHostFilter {
public:
  AsyncTriggerFilter(int num_reads_expected, int num_writes_expected)
      : num_reads_remaining_(num_reads_expected), num_writes_remaining_(num_writes_expected) {}

  ~AsyncTriggerFilter() override {
    ASSERT(num_reads_remaining_ == 0);
    ASSERT(num_writes_remaining_ == 0);
  }

  void resumeRead(UdpRecvData& data) {
    ASSERT(read_blocked_);
    read_blocked_ = false;
    read_callbacks().injectDatagramToFilterChain(data);
  }

  void resumeWrite(UdpRecvData& data) {
    ASSERT(write_blocked_);
    write_blocked_ = false;
    write_callbacks().injectDatagramToFilterChain(data);
  }

  bool any_blocked() const { return read_blocked_ || write_blocked_; }
  bool read_blocked() const { return read_blocked_; }
  bool write_blocked() const { return write_blocked_; }
  int num_reads_remaining() const { return num_reads_remaining_; }
  int num_writes_remaining() const { return num_writes_remaining_;}

  // Network::UpstreamDatagramHostFilter

  FilterStatus onRead(UdpRecvData&) override {
    ASSERT(!read_blocked_);
    ASSERT(num_reads_remaining_ > 0);

    read_blocked_ = true;
    --num_reads_remaining_;

    return FilterStatus::StopIteration;
  }

  FilterStatus onWrite(UdpRecvData&) override {
    ASSERT(!write_blocked_);
    ASSERT(num_writes_remaining_ > 0);

    write_blocked_ = true;
    --num_writes_remaining_;

    return FilterStatus::StopIteration;
  }

private:
  int num_reads_remaining_;
  bool read_blocked_ = false;
  int num_writes_remaining_;
  bool write_blocked_ = false;
};

// A silly arbitrary datagram payload mutation that assumes the data payload in `data` represents a
// simple integer and increments that integer by `increment`. Useful to validate that a filter was
// invoked and can mutate the filtered datagram.
void incrementPayload(int increment, UdpRecvData& data) {
  int num;
  bool is_num = absl::SimpleAtoi(data.buffer_->toString(), &num);
  ASSERT(is_num);

  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(std::to_string(num + increment));
}

MATCHER_P(IsEquivalentToDatagram, datagram_ptr, "") {
  return arg.addresses_ == datagram_ptr->addresses_ &&
         arg.buffer_->toString() == datagram_ptr->buffer_->toString() &&
         arg.receive_time_ == datagram_ptr->receive_time_ &&
         arg.tos_ == datagram_ptr->tos_ &&
         arg.saved_cmsg_.toString() == datagram_ptr->saved_cmsg_.toString();
}

TEST_F(DatagramHostImplTest, NoFilters) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
      {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
      std::make_unique<Buffer::OwnedImpl>("data"),
      time_system_.monotonicTime(), 0, {}};

  datagram_host.write(datagram);

  // Without any filters, expect the write to noop and never receive any response datagrams.
  EXPECT_THAT(received_datagrams_, IsEmpty());
}

TEST_F(DatagramHostImplTest, SimpleTerminalFilter) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
        {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
        std::make_unique<Buffer::OwnedImpl>("data"),
        time_system_.monotonicTime(), 0, {}};
  UdpRecvData expected_response{
          {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
          std::make_unique<Buffer::OwnedImpl>("data"),
          time_system_.monotonicTime(), 0, {}};

  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, ReadWriteFilter) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
          {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
          std::make_unique<Buffer::OwnedImpl>("5"),
          time_system_.monotonicTime(), 0, {}};

  auto filter = std::make_shared<MockUpstreamDatagramHostFilter>();
  EXPECT_CALL(*filter, initializeReadFilterCallbacks(_));
  EXPECT_CALL(*filter, initializeWriteFilterCallbacks(_));
  {
    InSequence seq;
    EXPECT_CALL(*filter, onWrite(Ref(datagram)))
        .WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 1)),
                        Return(FilterStatus::Continue)));
    EXPECT_CALL(*filter, onRead(_)).WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 10)),
                                                   Return(FilterStatus::Continue)));
  }

  // 5 + 1 + 10
  UdpRecvData expected_response{
            {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
            std::make_unique<Buffer::OwnedImpl>("16"),
            time_system_.monotonicTime(), 0, {}};

  datagram_host.addFilter(filter);
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_EQ(datagram.buffer_->toString(), "6");
  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, ReadOnlyFilters) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
            {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
            std::make_unique<Buffer::OwnedImpl>("5"),
            time_system_.monotonicTime(), 0, {}};

  auto filter1 = std::make_shared<MockUpstreamDatagramHostFilter>();
  EXPECT_CALL(*filter1, initializeReadFilterCallbacks(_));
  auto filter2 = std::make_shared<MockUpstreamDatagramHostFilter>();
  EXPECT_CALL(*filter2, initializeReadFilterCallbacks(_));
  {
    // Expect read filters to fire in opposite of added order.
    InSequence seq;
    EXPECT_CALL(*filter2, onRead(_)).WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 6)),
                                                    Return(FilterStatus::Continue)));
    EXPECT_CALL(*filter1, onRead(_)).WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 3)),
                                                    Return(FilterStatus::Continue)));
  }

  // 5 + 6 + 3
  UdpRecvData expected_response{
              {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
              std::make_unique<Buffer::OwnedImpl>("14"),
              time_system_.monotonicTime(), 0, {}};

  datagram_host.addReadFilter(filter1);
  datagram_host.addReadFilter(filter2);
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_EQ(datagram.buffer_->toString(), "5");
  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, WriteOnlyFilters) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
              {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
              std::make_unique<Buffer::OwnedImpl>("5"),
              time_system_.monotonicTime(), 0, {}};

  auto filter1 = std::make_shared<MockUpstreamDatagramHostFilter>();
  EXPECT_CALL(*filter1, initializeWriteFilterCallbacks(_));
  auto filter2 = std::make_shared<MockUpstreamDatagramHostFilter>();
  EXPECT_CALL(*filter2, initializeWriteFilterCallbacks(_));
  {
    // Expect write filters to fire in added order.
    InSequence seq;
    EXPECT_CALL(*filter1, onWrite(Ref(datagram)))
        .WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 10)),
                        Return(FilterStatus::Continue)));
    EXPECT_CALL(*filter2, onWrite(_)).WillOnce(DoAll(Invoke(absl::bind_front(incrementPayload, 2)),
                                                     Return(FilterStatus::Continue)));
  }

  // 5 + 10 + 2
  UdpRecvData expected_response{
                {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
                std::make_unique<Buffer::OwnedImpl>("17"),
                time_system_.monotonicTime(), 0, {}};

  datagram_host.addWriteFilter(filter1);
  datagram_host.addWriteFilter(filter2);
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_EQ(datagram.buffer_->toString(), "17");
  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, AsyncFilters) {
  DatagramHostImpl datagram_host(host_, this);

  UdpRecvData datagram{
            {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
            std::make_unique<Buffer::OwnedImpl>("data"),
            time_system_.monotonicTime(), 0, {}};

  auto filter1 = std::make_shared<AsyncTriggerFilter>(/*num_reads_expected=*/0,
                                                      /*num_writes_expected=*/1);
  auto filter2 = std::make_shared<AsyncTriggerFilter>(/*num_reads_expected=*/1,
                                                      /*num_writes_expected=*/0);
  auto filter3 = std::make_shared<AsyncTriggerFilter>(/*num_reads_expected=*/1,
                                                      /*num_writes_expected=*/1);

  EXPECT_CALL(*filter1, initializeWriteFilterCallbacks(_));
  EXPECT_CALL(*filter2, initializeReadFilterCallbacks(_));
  EXPECT_CALL(*filter3, initializeReadFilterCallbacks(_));
  EXPECT_CALL(*filter3, initializeWriteFilterCallbacks(_));

  // Install `filter1` as a write-only filter, then `filter2` as a read-only filter, then `filter3`
  // as a read/write filter.
  datagram_host.addWriteFilter(filter1);
  datagram_host.addReadFilter(filter2);
  datagram_host.addFilter(filter3);
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));

  ASSERT_FALSE(filter1->any_blocked());
  ASSERT_FALSE(filter2->any_blocked());
  ASSERT_FALSE(filter3->any_blocked());

  datagram_host.write(datagram);

  // Expect a write to `filter1`.
  EXPECT_EQ(filter1->num_writes_remaining(), 0);
  ASSERT_TRUE(filter1->write_blocked());
  EXPECT_FALSE(filter2->any_blocked());
  EXPECT_FALSE(filter3->any_blocked());

  filter1->resumeWrite(datagram);

  // Expect write to `filter3`.
  EXPECT_FALSE(filter1->any_blocked());
  EXPECT_FALSE(filter2->any_blocked());
  EXPECT_EQ(filter3->num_writes_remaining(), 0);
  ASSERT_TRUE(filter3->write_blocked());
  EXPECT_FALSE(filter3->read_blocked());

  filter3->resumeWrite(datagram);

  // Expect read to `filter3`.
  EXPECT_FALSE(filter1->any_blocked());
  EXPECT_FALSE(filter2->any_blocked());
  EXPECT_FALSE(filter3->write_blocked());
  EXPECT_EQ(filter3->num_reads_remaining(), 0);
  ASSERT_TRUE(filter3->read_blocked());

  filter3->resumeRead(datagram);

  // Expect read to `filter2`.
  EXPECT_FALSE(filter1->any_blocked());
  EXPECT_FALSE(filter2->write_blocked());
  EXPECT_EQ(filter2->num_reads_remaining(), 0);
  ASSERT_TRUE(filter2->read_blocked());
  EXPECT_FALSE(filter3->any_blocked());

  filter2->resumeRead(datagram);

  EXPECT_FALSE(filter1->any_blocked());
  EXPECT_FALSE(filter2->any_blocked());
  EXPECT_FALSE(filter3->any_blocked());
  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&datagram)));
}

}  // namespace
}  // namespace Envoy::Upstream
