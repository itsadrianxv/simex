#include <cassert>
#include <chrono>
#include <cstdint>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "transport/exchange_service.h"
#include "transport/protocol.h"

static auto instrument() -> simex::common::InstrumentConfig {
  simex::common::InstrumentConfig config;
  config.symbol = "RB"; config.exchange = "SHFE"; config.tick_size = 1;
  config.price_limit_percent = 0.1; config.max_price_levels = 1000;
  config.supported_order_combinations = {{simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY}};
  return config;
}

int main() {
  simex::transport::ExchangeServiceConfig config;
  config.runtime.instrument = instrument();
  config.runtime.reference_price = 1000;
  config.runtime.trading_day = 1;
  config.runtime.initial_phase = simex::common::SessionPhase::CONTINUOUS;
  config.runtime.clock_mode = simex::runtime::ClockMode::REALTIME;
  config.runtime.sessions = {{500000000, 1, simex::common::SessionPhase::CLOSED}};
  config.runtime.queue_capacity = 16;
  config.udp_destination_port = 9;
  simex::transport::ExchangeService service(config);
  assert(service.start());
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  timeval timeout{3, 0};
  assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(service.tcpPort());
  assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  simex::transport::WireRequest request{};
  request.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::REQUEST);
  request.header.payload_size = sizeof(request.request);
  request.request.client_id = 1; request.request.ticker_id = 0;
  request.request.client_order_id = 7; request.request.price_ticks = 1000;
  request.request.qty = 1;
  // Exercise fragmented TCP framing, then remain idle while the session closes.
  const auto* bytes = reinterpret_cast<const char*>(&request);
  assert(::send(fd, bytes, 3, 0) == 3);
  assert(::send(fd, bytes + 3, sizeof(request) - 3, 0) == static_cast<ssize_t>(sizeof(request) - 3));
  auto receive = [&] {
    simex::transport::WireResponse response{};
    std::size_t got = 0;
    while (got < sizeof(response)) {
      const auto count = ::recv(fd, reinterpret_cast<char*>(&response) + got, sizeof(response) - got, 0);
      assert(count > 0);
      got += static_cast<std::size_t>(count);
    }
    return response;
  };
  auto response = receive();
  assert(response.header.sequence == 0);
  assert(response.response.type == simex::common::ResponseType::ACCEPTED);
  response = receive();
  assert(response.header.sequence == 1);
  assert(response.response.type == simex::common::ResponseType::CANCELED);
  assert(response.response.reason == simex::common::ReasonCode::SESSION_END);
  const auto before = std::chrono::steady_clock::now();
  service.stop();  // Must unblock even though the participant socket is still open.
  assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
  ::close(fd);
}
