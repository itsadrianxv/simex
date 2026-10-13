#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <sys/epoll.h>

#include "transport/tcp_socket.h"

namespace simex::transport {

/// Generic epoll(7) TCP server: edge-triggered, non-blocking, N connections,
/// per-connection buffering in TCPSocket.
///
/// The server is a passive engine: it owns the epoll instance, the listener,
/// and connection lifetimes, and exposes poll()/sendAndRecv() rounds for the
/// owner's loop. Protocol framing and connection policy live with the owner
/// through the recv/accept/disconnect callbacks.
class TCPServer final {
 public:
  /// Return false to reject (immediately close) a newly accepted connection.
  using AcceptPolicy = std::function<bool(TCPSocket *)>;
  using DisconnectCallback = std::function<void(TCPSocket *)>;

  TCPServer() = default;
  ~TCPServer();
  TCPServer(const TCPServer &) = delete;
  auto operator=(const TCPServer &) -> TCPServer & = delete;

  /// Create the epoll instance, bind 127.0.0.1 and listen. Port 0 selects an
  /// ephemeral port; read the chosen port via boundPort().
  auto start(std::uint16_t port) -> bool;
  auto stop() -> void;

  /// Wait for events up to timeout_ms (0 = non-blocking slice), accept pending
  /// connections, and flag readable edges on connections. Returns false on a
  /// fatal epoll or accept error.
  auto poll(int timeout_ms) -> bool;

  /// Drain flagged sockets (recv callback), flush outbound buffers
  /// opportunistically, and reap dead or peer-closed connections.
  auto sendAndRecv() -> void;

  /// Register an external descriptor (for example an eventfd) whose events
  /// are delivered to callback. Must be called before the owner's event-loop
  /// thread starts touching poll()/sendAndRecv().
  auto registerExternalFd(int fd, std::uint32_t events,
                          std::function<void(std::uint32_t revents)> callback) -> bool;

  [[nodiscard]] auto boundPort() const noexcept -> std::uint16_t { return bound_port_; }
  [[nodiscard]] auto connectionCount() const noexcept -> std::size_t;

  /// Set by the owner before start(); propagated to every accepted socket.
  TCPSocket::RecvCallback recv_callback_;
  AcceptPolicy accept_policy_;
  DisconnectCallback disconnect_callback_;

 private:
  struct Registration final {
    enum class Kind : std::uint8_t { kListener, kConnection, kExternal };
    Kind kind = Kind::kListener;
    int fd = -1;
    std::unique_ptr<TCPSocket> socket;
    std::function<void(std::uint32_t)> external_callback;
  };

  auto acceptPending() -> bool;
  auto addRegistration(std::unique_ptr<Registration> registration, std::uint32_t events) -> bool;
  auto closeRegistration(std::size_t index, bool notify) -> void;

  int epoll_fd_ = -1;
  int listen_fd_ = -1;
  std::uint16_t bound_port_ = 0;
  std::vector<epoll_event> events_;
  std::vector<std::unique_ptr<Registration>> registrations_;
};

}  // namespace simex::transport
