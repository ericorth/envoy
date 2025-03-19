#include "source/common/upstream/datagram_host_impl.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/network/listener.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy::Upstream {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::NiceMock;

class DatagramHostImplTest : public testing::Test,
                             public DatagramHost::Callbacks {
 public:
  ~DatagramHostImplTest() override = default;

  // DatagramHost::Callbacks
  void onDatagramRead(Network::UdpRecvData& data) override {
    Network::UdpRecvData copy{data.addresses_, std::make_unique<Buffer::OwnedImpl>(*data.buffer_),
                              data.receive_time_, data.tos_, {}};
    copy.saved_cmsg_.add(data.saved_cmsg_);
    received_datagrams_.push_back(std::move(copy));
  }

  std::vector<Network::UdpRecvData> received_datagrams_;

  std::shared_ptr<MockClusterInfo> cluster_ = std::make_shared<NiceMock<MockClusterInfo>>();
  Event::SimulatedTimeSystem time_system_;
  HostSharedPtr host_ = makeTestHost(cluster_, "hostname", "udp://127.0.0.1:80", time_system_);
};


// Write filter that takes the write datagram and reflects back a copy with local and peer addresses
// reversed and an updated receive time.
class ReflectDatagramFilter : public Network::UpstreamDatagramHostWriteFilter {
  public:
   ReflectDatagramFilter(TimeSource* time_source) : time_source_(time_source) {
     ASSERT(time_source_);
   }

   ~ReflectDatagramFilter() override = default;

   // Network::UpstreamDatagramHostWriteFilter

  Network::FilterStatus onWrite(Network::UdpRecvData& data) override {
    ASSERT(callbacks_);

    Network::UdpRecvData copy{{data.addresses_.peer_, data.addresses_.local_},
                              std::make_unique<Buffer::OwnedImpl>(*data.buffer_),
                              time_source_->monotonicTime(), data.tos_, {}};
    copy.saved_cmsg_.add(data.saved_cmsg_);
    callbacks_->readData(copy);

    return Network::FilterStatus::StopIteration;
  }

  void initializeWriteFilterCallbacks(
      Network::UpstreamDatagramHostWriteFilterCallbacks* filter_callbacks) override {
    ASSERT(!callbacks_);
    ASSERT(filter_callbacks);
    callbacks_ = filter_callbacks;
  }

  private:
    TimeSource* time_source_;
    Network::UpstreamDatagramHostWriteFilterCallbacks* callbacks_ = nullptr;
};

// Read/Write filter that assumes datagram payload buffer represents a simple integer and updates
// the buffer by incrementing that integer (by the given amounts).
class IncrementingFilter : public Network::UpstreamDatagramHostFilter {
  public:
  IncrementingFilter(int write_increment, int read_increment) : write_increment_(write_increment),
                                                                read_increment_(read_increment) {}

  ~IncrementingFilter() override = default;

  // Network::UpstreamDatagramHostFilter

  Network::FilterStatus onRead(Network::UdpRecvData& data) override {
    incrementPayload(data, read_increment_);
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    ASSERT(false);
    return Network::FilterStatus::StopIteration;
  }

  void initializeReadFilterCallbacks(Network::UpstreamDatagramHostReadFilterCallbacks*) override {}

  Network::FilterStatus onWrite(Network::UdpRecvData& data) override {
    incrementPayload(data, write_increment_);
    return Network::FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(
      Network::UpstreamDatagramHostWriteFilterCallbacks*) override {}

  private:
  void incrementPayload(Network::UdpRecvData& data, int increment) {
    int num;
    bool is_num = absl::SimpleAtoi(data.buffer_->toString(), &num);
    ASSERT(is_num);

    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(std::to_string(num + increment));
  }

  int write_increment_;
  int read_increment_;
};

class EquivalentDatagramMatcher : public MatcherInterface<const Network::UdpRecvData&> {
  public:
  explicit EquivalentDatagramMatcher(const Network::UdpRecvData* datagram) : datagram_(datagram) {
    ASSERT(datagram_);
  }

  bool MatchAndExplain(const Network::UdpRecvData& value, MatchResultListener*) const override {
    return value.addresses_ == datagram_->addresses_ &&
           value.buffer_->toString() == datagram_->buffer_->toString() &&
           value.receive_time_ == datagram_->receive_time_ &&
           value.tos_ == datagram_->tos_ &&
           value.saved_cmsg_.toString() == datagram_->saved_cmsg_.toString();
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is equivalent to datagram";
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "is not equivalent to datagram";
  }

  private:
  const Network::UdpRecvData* datagram_;
};

Matcher<Network::UdpRecvData> IsEquivalentToDatagram(const Network::UdpRecvData* datagram) {
  return Matcher<Network::UdpRecvData>(new EquivalentDatagramMatcher(datagram));
}

TEST_F(DatagramHostImplTest, NoFilters) {
  DatagramHostImpl datagram_host(host_, this);

  Network::UdpRecvData datagram{
      {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
      std::make_unique<Buffer::OwnedImpl>("data"),
      time_system_.monotonicTime(), 0, {}};

  datagram_host.write(datagram);

  // Without any filters, expect the write to noop and never receive any response datagrams.
  EXPECT_THAT(received_datagrams_, IsEmpty());
}

TEST_F(DatagramHostImplTest, SimpleTerminalFilter) {
  DatagramHostImpl datagram_host(host_, this);

  Network::UdpRecvData datagram{
        {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
        std::make_unique<Buffer::OwnedImpl>("data"),
        time_system_.monotonicTime(), 0, {}};
  Network::UdpRecvData expected_response{
          {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
          std::make_unique<Buffer::OwnedImpl>("data"),
          time_system_.monotonicTime(), 0, {}};

  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, ReadWriteFilter) {
  DatagramHostImpl datagram_host(host_, this);

  Network::UdpRecvData datagram{
          {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
          std::make_unique<Buffer::OwnedImpl>("5"),
          time_system_.monotonicTime(), 0, {}};
  Network::UdpRecvData expected_response{
            {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
            std::make_unique<Buffer::OwnedImpl>("16"),
            time_system_.monotonicTime(), 0, {}};

  datagram_host.addFilter(std::make_shared<IncrementingFilter>(/*write_increment=*/1,
                                                               /*read_increment=*/10));
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, ReadOnlyFilter) {
  DatagramHostImpl datagram_host(host_, this);

  Network::UdpRecvData datagram{
            {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
            std::make_unique<Buffer::OwnedImpl>("5"),
            time_system_.monotonicTime(), 0, {}};
  Network::UdpRecvData expected_response{
              {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
              std::make_unique<Buffer::OwnedImpl>("15"),
              time_system_.monotonicTime(), 0, {}};

  datagram_host.addReadFilter(std::make_shared<IncrementingFilter>(/*write_increment=*/1,
                                                                   /*read_increment=*/10));
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

TEST_F(DatagramHostImplTest, WriteOnlyFilter) {
  DatagramHostImpl datagram_host(host_, this);

  Network::UdpRecvData datagram{
              {Network::Utility::getIpv6AnyAddress(), Network::Utility::getIpv6LoopbackAddress()},
              std::make_unique<Buffer::OwnedImpl>("5"),
              time_system_.monotonicTime(), 0, {}};
  Network::UdpRecvData expected_response{
                {Network::Utility::getIpv6LoopbackAddress(), Network::Utility::getIpv6AnyAddress()},
                std::make_unique<Buffer::OwnedImpl>("6"),
                time_system_.monotonicTime(), 0, {}};

  datagram_host.addWriteFilter(std::make_shared<IncrementingFilter>(/*write_increment=*/1,
                                                                    /*read_increment=*/10));
  datagram_host.addWriteFilter(std::make_shared<ReflectDatagramFilter>(&time_system_));
  datagram_host.write(datagram);

  EXPECT_THAT(received_datagrams_, ElementsAre(IsEquivalentToDatagram(&expected_response)));
}

}  // namespace
}  // namespace Envoy::Upstream
