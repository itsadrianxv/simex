#include "transport/tcp_order_gateway.h"
#include "transport/protocol.h"

#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace simex::transport {
TcpOrderGateway::~TcpOrderGateway() { stop(); }

auto TcpOrderGateway::start(RequestHandler handler, ResponseSource source) -> bool {
  if (thread_.joinable()) return false;
  request_handler_ = std::move(handler);
  response_source_ = std::move(source);
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) return false;
  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(endpoint_.port);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
      ::listen(listen_fd_, 1) < 0) {
    ::close(listen_fd_); listen_fd_ = -1; return false;
  }
  socklen_t length = sizeof(address);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
    ::close(listen_fd_); listen_fd_ = -1; return false;
  }
  bound_port_ = ntohs(address.sin_port);
  stopping_.store(false);
  healthy_.store(true);
  peer_closed_.store(false);
  thread_ = std::thread([this] { run(); });
  return true;
}

auto TcpOrderGateway::stop() -> void {
  stopping_.store(true);
  if (thread_.joinable()) thread_.join();
  if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
}

void TcpOrderGateway::run() {
  int client = -1;
  try {
    while (!stopping_.load() && client < 0) {
      pollfd descriptor{listen_fd_, POLLIN, 0};
      const auto result = ::poll(&descriptor, 1, 10);
      if (result < 0 && errno != EINTR) throw std::runtime_error("TCP accept poll failed");
      if (result > 0) {
        client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0 && errno != EAGAIN && errno != EINTR)
          throw std::runtime_error("TCP accept failed");
      }
    }
    WireRequest input{};
    WireResponse output{};
    std::size_t received = 0, sent = 0;
    bool pending_output = false;
    std::uint64_t expected_sequence = 0;
    auto last_progress = std::chrono::steady_clock::now();
    while (!stopping_.load() && client >= 0) {
      if (!pending_output && response_source_) {
        simex::exchange::ClientResponse response;
        std::uint64_t sequence = 0;
        if (response_source_(&response, &sequence)) {
          output = {};
          output.header.kind = static_cast<std::uint8_t>(MessageKind::RESPONSE);
          output.header.payload_size = sizeof(output.response);
          output.header.sequence = sequence;
          output.response = response;
          pending_output = true;
          sent = 0;
          last_progress = std::chrono::steady_clock::now();
        }
      }
      pollfd descriptor{client, static_cast<short>(POLLIN | (pending_output ? POLLOUT : 0)), 0};
      const auto result = ::poll(&descriptor, 1, 2);
      if (result < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("TCP poll failed");
      }
      if (descriptor.revents & POLLHUP) { peer_closed_.store(true); break; }
      if (descriptor.revents & (POLLERR | POLLNVAL))
        throw std::runtime_error("TCP participant disconnected");
      if (descriptor.revents & POLLIN) {
        const auto count = ::recv(client, reinterpret_cast<char*>(&input) + received,
                                  sizeof(input) - received, 0);
        if (count == 0) { peer_closed_.store(true); break; }
        if (count < 0 && errno != EAGAIN && errno != EINTR)
          throw std::runtime_error("TCP read failed");
        if (count > 0) received += static_cast<std::size_t>(count);
        if (received == sizeof(input)) {
          if (input.header.version != kProtocolVersion || input.header.reserved != 0 ||
              input.header.kind != static_cast<std::uint8_t>(MessageKind::REQUEST) ||
              input.header.payload_size != sizeof(input.request) ||
              input.header.sequence != expected_sequence ||
              !request_handler_ || !request_handler_(input.request, expected_sequence))
            throw std::runtime_error("Invalid TCP request or runtime unavailable");
          ++expected_sequence;
          received = 0;
          input = {};
        }
      }
      if (pending_output && (descriptor.revents & POLLOUT)) {
        const auto count = ::send(client, reinterpret_cast<const char*>(&output) + sent,
                                  sizeof(output) - sent, MSG_NOSIGNAL);
        if (count < 0 && errno != EAGAIN && errno != EINTR)
          throw std::runtime_error("TCP write failed");
        if (count > 0) { sent += static_cast<std::size_t>(count); last_progress = std::chrono::steady_clock::now(); }
        if (sent == sizeof(output)) pending_output = false;
      }
      if (pending_output && std::chrono::steady_clock::now() - last_progress > std::chrono::seconds(5))
        throw std::runtime_error("TCP participant is not reading responses");
    }
  } catch (...) { healthy_.store(false); }
  if (peer_closed_.load()) healthy_.store(false);
  if (client >= 0) ::close(client);
}
}  // namespace simex::transport
