#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "exchange/messages.h"
#include "transport/tcp_server.h"

namespace simex::transport {

struct Endpoint final { std::uint16_t port = 0; };

/// TCP front-end for the order server. Owns the protocol layer on top of a
/// TCPServer epoll loop running on a dedicated thread: fixed-size frame
/// reassembly with sequence validation, single-session admission (further
/// connections are closed immediately), a five-second response watchdog, and
/// eventfd-based immediate shutdown.
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
  [[nodiscard]] auto boundPort() const noexcept -> std::uint16_t { return server_.boundPort(); }
  [[nodiscard]] auto healthy() const noexcept -> bool { return healthy_.load(); }
  [[nodiscard]] auto peerClosed() const noexcept -> bool { return peer_closed_.load(); }
 private:
  void run();
  auto admitSession(TCPSocket *socket) -> bool;
  void handleRecv(TCPSocket *socket);
  void handleDisconnect(TCPSocket *socket);

  Endpoint endpoint_;
  RequestHandler request_handler_;
  ResponseSource response_source_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> healthy_{true};
  std::atomic<bool> peer_closed_{false};
  // TODO: replace stopping_ flag with std::stop_token plumbing.
  std::jthread thread_;
  TCPServer server_;
  int event_fd_ = -1;

  // Session state is only touched by the event-loop thread.
  TCPSocket *session_ = nullptr;
  bool session_seen_ = false;
  std::uint64_t expected_sequence_ = 0;
};

}  // namespace simex::transport
