#include "transport/tcp_order_gateway.h"
#include "transport/protocol.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace simex::transport {
namespace {
auto readAll(int fd, void *buffer, std::size_t size) -> bool {
  auto *bytes = static_cast<char *>(buffer); std::size_t offset = 0;
  while (offset < size) { const auto n = ::recv(fd, bytes + offset, size - offset, 0); if (n <= 0) return false; offset += static_cast<std::size_t>(n); }
  return true;
}
auto writeAll(int fd, const void *buffer, std::size_t size) -> bool {
  const auto *bytes = static_cast<const char *>(buffer); std::size_t offset = 0;
  while (offset < size) { const auto n = ::send(fd, bytes + offset, size - offset, 0); if (n <= 0) return false; offset += static_cast<std::size_t>(n); }
  return true;
}
}
TcpOrderGateway::~TcpOrderGateway() { stop(); }
auto TcpOrderGateway::start(RequestHandler request_handler, ResponseSource response_source) -> bool {
#ifdef _WIN32
  (void)request_handler; (void)response_source; return false;
#else
  if (thread_.joinable()) return false;
  request_handler_ = std::move(request_handler); response_source_ = std::move(response_source);
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0); if (listen_fd_ < 0) return false;
  int one = 1; ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(endpoint_.port);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || ::listen(listen_fd_, 1) < 0) { ::close(listen_fd_); listen_fd_ = -1; return false; }
  sockaddr_in bound{}; socklen_t length = sizeof(bound); ::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&bound), &length); bound_port_ = ntohs(bound.sin_port);
  stopping_.store(false); thread_ = std::thread([this] { run(); }); return true;
#endif
}
auto TcpOrderGateway::stop() -> void {
#ifndef _WIN32
  stopping_.store(true); if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); ::close(listen_fd_); listen_fd_ = -1; }
#endif
  if (thread_.joinable()) thread_.join();
}
void TcpOrderGateway::run() {
#ifndef _WIN32
  const auto client = ::accept(listen_fd_, nullptr, nullptr); if (client < 0) return;
  std::uint64_t expected_sequence = 0;
  while (!stopping_.load()) {
    WireRequest wire{}; if (!readAll(client, &wire, sizeof(wire))) break;
    if (wire.header.version != kProtocolVersion || wire.header.kind != static_cast<std::uint8_t>(MessageKind::REQUEST) || wire.header.payload_size != sizeof(wire.request) || wire.header.sequence != expected_sequence) break;
    if (request_handler_ && !request_handler_(wire.request, wire.header.sequence)) break;
    ++expected_sequence;
    if (response_source_) { simex::exchange::ClientResponse response; std::uint64_t sequence = 0; while (response_source_(&response, &sequence)) { WireResponse out{}; out.header.kind = static_cast<std::uint8_t>(MessageKind::RESPONSE); out.header.payload_size = sizeof(out.response); out.header.sequence = sequence; out.response = response; if (!writeAll(client, &out, sizeof(out))) break; } }
  }
  ::shutdown(client, SHUT_RDWR); ::close(client);
#endif
}
}  // namespace simex::transport
