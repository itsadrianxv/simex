#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "exchange/messages.h"

namespace simex::transport {

struct Endpoint final { std::uint16_t port = 0; };

class TcpOrderGateway final {
 public:
  using RequestHandler = std::function<bool(const simex::exchange::ClientRequest &, std::uint64_t)>;
  using ResponseSource = std::function<bool(simex::exchange::ClientResponse *, std::uint64_t *)>;
  explicit TcpOrderGateway(Endpoint endpoint) : endpoint_(endpoint) {}
  ~TcpOrderGateway();
  TcpOrderGateway(const TcpOrderGateway &) = delete;
  auto operator=(const TcpOrderGateway &) -> TcpOrderGateway & = delete;
  auto start(RequestHandler request_handler, ResponseSource response_source) -> bool;
  auto stop() -> void;
  [[nodiscard]] auto boundPort() const noexcept -> std::uint16_t { return bound_port_; }
  [[nodiscard]] auto healthy() const noexcept -> bool { return healthy_.load(); }
  [[nodiscard]] auto peerClosed() const noexcept -> bool { return peer_closed_.load(); }
 private:
  void run();
  Endpoint endpoint_;
  std::uint16_t bound_port_ = 0;
  RequestHandler request_handler_;
  ResponseSource response_source_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> healthy_{true};
  std::atomic<bool> peer_closed_{false};
  std::thread thread_;
  int listen_fd_ = -1;
};

}  // namespace simex::transport
