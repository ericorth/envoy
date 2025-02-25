#pragma once

#include "envoy/common/pure.h"

namespace Envoy::Udp {

class DatagramHost {
  public:
  void write(Network::UdpRecvData& data);
};

class DatagramCallbacks {
  public:
  virtual void onDatagramRead(Network::UdpRecvData& data) PURE;
};


}  // namespace Envoy::Udp
