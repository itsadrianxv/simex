#include "transport/tcp_socket.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace simex::transport {
namespace {
auto nowNanos() noexcept -> simex::common::Nanos {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

TCPSocket::TCPSocket(int fd, RecvCallback recv_callback)
    : fd_(fd),
      recv_callback_(std::move(recv_callback)),
      inbound_data_(kBufferSize),
      outbound_data_(kBufferSize) {}

TCPSocket::~TCPSocket() {
  if (fd_ >= 0) ::close(fd_);
}

auto TCPSocket::consume(std::size_t n) noexcept -> void {
  if (n > next_rcv_valid_index_) n = next_rcv_valid_index_;
  next_rcv_valid_index_ -= n;
  if (next_rcv_valid_index_ > 0)
    std::memmove(inbound_data_.data(), inbound_data_.data() + n, next_rcv_valid_index_);
}

auto TCPSocket::send(const void *data, std::size_t len) noexcept -> bool {
  if (dead_ || len > kBufferSize - next_send_valid_index_) return false;
  std::memcpy(outbound_data_.data() + next_send_valid_index_, data, len);
  next_send_valid_index_ += len;
  return true;
}

auto TCPSocket::deliverInbound() noexcept -> bool {
  if (next_rcv_valid_index_ == 0 || !recv_callback_) return true;
  const auto before = next_rcv_valid_index_;
  recv_callback_(this, nowNanos());
  return next_rcv_valid_index_ < before;
}

auto TCPSocket::flushOutbound() noexcept -> bool {
  bool sent_any = false;
  while (next_send_valid_index_ > 0) {
    const auto n = ::send(fd_, outbound_data_.data(), next_send_valid_index_, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n > 0) {
      sent_any = true;
      const auto sent = static_cast<std::size_t>(n);
      next_send_valid_index_ -= sent;
      if (next_send_valid_index_ > 0)
        std::memmove(outbound_data_.data(), outbound_data_.data() + sent, next_send_valid_index_);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    if (n < 0 && errno == EINTR) break;
    dead_ = true;
    break;
  }
  return sent_any;
}

auto TCPSocket::sendAndRecv() noexcept -> Round {
  Round round;
  if (dead_) {
    last_round_ = round;
    return round;
  }
  if (want_recv_ && !peer_closed_) {
    want_recv_ = false;
    for (;;) {
      if (next_rcv_valid_index_ == kBufferSize && !deliverInbound()) {
        dead_ = true;
        break;
      }
      const auto n = ::recv(fd_, inbound_data_.data() + next_rcv_valid_index_,
                            kBufferSize - next_rcv_valid_index_, MSG_DONTWAIT);
      if (n > 0) {
        next_rcv_valid_index_ += static_cast<std::size_t>(n);
        round.read_any = true;
        continue;
      }
      if (n == 0) {
        peer_closed_ = true;
        break;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
      dead_ = true;
      break;
    }
    deliverInbound();
  }
  if (!dead_) round.sent_any = flushOutbound();
  last_round_ = round;
  return round;
}

}  // namespace simex::transport
