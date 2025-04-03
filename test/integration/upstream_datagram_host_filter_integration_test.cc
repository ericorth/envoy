#include "gtest/gtest.h"
#include "test/integration/integration.h"

namespace Envoy {
namespace {

//!!
constexpr absl::string_view kListenerFilterConfig = "";

// A simple test datagram listener filter that always passes received datagrams to the upstream via
// a DatagramHost handler.
class TestDatagramListenerFilter : public Network::UdpListenerReadFilter {
public:
  TestDatagramListenerFilter(Network::UdpReadFilterCallbacks& callbacks)
      : UdpListenerReadFilter(callbacks) {}

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& /*data*/) override {
    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    ASSERT(false, "Listener errors not expected.");
    return Network::FilterStatus::StopIteration;
  }
};

class TestDatagramListenerFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
  public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager,
              Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<TestDatagramListenerFilter>(callbacks));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "test.udp_listener.test"; }
};

class UpstreamDatagramHostFilterIntegrationTest : public BaseIntegrationTest {
public:
  void initialize() override {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());

    std::string listener_filter_config(kListenerFilterConfig);
    config_helper_.addListenerFilter(listener_filter_config);

    BaseIntegrationTest::initialize();
  }
};

} // namespace
} // namespace Envoy
