#include <cassert>
#include <chrono>
#include <thread>
#include "transport/protocol.h"
#include "transport/tcp_order_gateway.h"
#include "transport/udp_market_data_publisher.h"
int main() {
  static_assert(sizeof(simex::transport::FrameHeader) == 16);
  simex::transport::TcpOrderGateway tcp({0});
  assert(tcp.start([](const simex::exchange::ClientRequest &, std::uint64_t) { return true; },
                   [](simex::exchange::ClientResponse *, std::uint64_t *) { return false; }));
  assert(tcp.boundPort() != 0); tcp.stop();
  simex::transport::UdpMarketDataPublisher udp(0, std::chrono::milliseconds(1));
  assert(udp.start([](simex::exchange::MarketUpdate *, std::uint64_t *) { return false; },
                   [](std::uint64_t *) { return false; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(2)); udp.stop();
  return 0;
}
