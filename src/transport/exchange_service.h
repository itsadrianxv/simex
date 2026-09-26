#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include "runtime/exchange_runtime.h"
#include "transport/tcp_order_gateway.h"
#include "transport/udp_market_data_publisher.h"
namespace simex::transport {
struct ExchangeServiceConfig final { simex::runtime::ExchangeRuntimeConfig runtime; Endpoint tcp{}; std::uint16_t udp_destination_port = 0; std::chrono::milliseconds snapshot_interval{1000}; simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID; };
class ExchangeService final {
 public:
  explicit ExchangeService(ExchangeServiceConfig config); ~ExchangeService();
  ExchangeService(const ExchangeService&) = delete; auto operator=(const ExchangeService&) -> ExchangeService& = delete;
  auto start() -> bool; auto stop() -> void;
  [[nodiscard]] auto ready() const noexcept -> bool { return started_.load() && runtime_.ready() && tcp_.healthy() && udp_.healthy(); }
  [[nodiscard]] auto tcpPort() const noexcept -> std::uint16_t { return tcp_.boundPort(); }
  [[nodiscard]] auto sessionEnded() const noexcept -> bool { return tcp_.peerClosed(); }
 private:
  auto onRequest(const simex::exchange::ClientRequest&, std::uint64_t) -> bool;
  auto nextResponse(simex::exchange::ClientResponse*, std::uint64_t*) -> bool;
  auto nextUpdate(simex::exchange::MarketUpdate*, std::uint64_t*) -> bool;
  auto snapshotSequence(simex::exchange::Snapshot*) -> bool;
  ExchangeServiceConfig config_; simex::runtime::ExchangeRuntime runtime_; TcpOrderGateway tcp_; UdpMarketDataPublisher udp_;
  std::atomic<bool> started_{false}; std::uint64_t response_sequence_ = 0; std::uint64_t market_sequence_ = 1;
};
}  // namespace simex::transport
