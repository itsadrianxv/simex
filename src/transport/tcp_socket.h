#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "common/types.h"

namespace simex::transport {

/// Per-connection TCP socket with preallocated inbound and outbound buffers.
///
/// The socket is used exclusively over edge-triggered epoll, which imposes
/// two disciplines: reads must drain to EAGAIN inside one round, and writes
/// are flushed opportunistically each round instead of waiting for an
/// EPOLLOUT registration.
class TCPSocket final {
 public:
  using RecvCallback = std::function<void(TCPSocket *, simex::common::Nanos rx_time)>;

  /// Per-round IO progress reported by sendAndRecv().
  struct Round final {
    bool read_any = false;
    bool sent_any = false;
  };

  /// Takes ownership of fd. The recv callback receives accumulated inbound
  /// bytes and consumes complete messages via consume().
  TCPSocket(int fd, RecvCallback recv_callback);
  ~TCPSocket();
  TCPSocket(const TCPSocket &) = delete;
  auto operator=(const TCPSocket &) -> TCPSocket & = delete;

  [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
  [[nodiscard]] auto inboundSize() const noexcept -> std::size_t { return next_rcv_valid_index_; }
  [[nodiscard]] auto inboundData() const noexcept -> const char * { return inbound_data_.data(); }
  [[nodiscard]] auto outboundPendingSize() const noexcept -> std::size_t { return next_send_valid_index_; }
  [[nodiscard]] auto peerClosed() const noexcept -> bool { return peer_closed_; }
  [[nodiscard]] auto dead() const noexcept -> bool { return dead_; }
  [[nodiscard]] auto lastRound() const noexcept -> Round { return last_round_; }

  /// Drop the first n buffered bytes and move the remainder to the front.
  auto consume(std::size_t n) noexcept -> void;

  /// Queue outbound bytes for the next sendAndRecv() round. Fails when the
  /// buffer lacks room or the socket is dead.
  auto send(const void *data, std::size_t len) noexcept -> bool;

  /// Mark the socket unusable after a protocol violation; the server reaps
  /// it on the same round.
  auto markDead() noexcept -> void { dead_ = true; }

  /// Flag a readable edge reported by the server.
  auto setWantRecv() noexcept -> void { want_recv_ = true; }

  /// Drain inbound data to EAGAIN (delivering the recv callback), then flush
  /// the outbound buffer until it is empty or the peer is not reading.
  auto sendAndRecv() noexcept -> Round;

 private:
  /// Hand buffered bytes to the recv callback. Returns false when the buffer
  /// is full and the callback consumed nothing (protocol jam).
  auto deliverInbound() noexcept -> bool;
  auto flushOutbound() noexcept -> bool;

  static constexpr std::size_t kBufferSize = 64 * 1024;  // TODO: the reference book preallocates 64 MiB; revisit if messages grow.

  int fd_;
  RecvCallback recv_callback_;
  bool want_recv_ = false;
  bool peer_closed_ = false;
  bool dead_ = false;
  std::size_t next_send_valid_index_ = 0;
  std::size_t next_rcv_valid_index_ = 0;
  Round last_round_{};
  std::vector<char> inbound_data_;
  std::vector<char> outbound_data_;
};

}  // namespace simex::transport
