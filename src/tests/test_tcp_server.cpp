#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include "transport/tcp_server.h"

namespace {

auto connectTo(std::uint16_t port) -> int {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  assert(::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
  return fd;
}

auto sendAll(int fd, const char *data, std::size_t size) -> void {
  std::size_t sent = 0;
  while (sent < size) {
    const auto count = ::send(fd, data + sent, size - sent, 0);
    assert(count > 0);
    sent += static_cast<std::size_t>(count);
  }
}

/// Accumulate exactly n bytes without blocking; returns false while data is
/// still in flight so the caller keeps pumping the server.
auto drainNonBlocking(int fd, std::string &out, std::size_t n) -> bool {
  char buffer[256];
  while (out.size() < n) {
    const auto count = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count < 0) return false;
    assert(count > 0);
    out.append(buffer, static_cast<std::size_t>(count));
  }
  return true;
}

template <typename Predicate>
auto pumpUntil(simex::transport::TCPServer &server, Predicate predicate) -> void {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!predicate()) {
    assert(std::chrono::steady_clock::now() < deadline);
    assert(server.poll(1));
    server.sendAndRecv();
  }
}

}  // namespace

int main() {
  simex::transport::TCPServer server;
  std::size_t disconnects = 0;
  server.recv_callback_ = [](simex::transport::TCPSocket *socket, simex::common::Nanos) {
    assert(socket->send(socket->inboundData(), socket->inboundSize()));
    socket->consume(socket->inboundSize());
  };
  server.disconnect_callback_ = [&disconnects](simex::transport::TCPSocket *) { ++disconnects; };
  assert(server.start(0));
  assert(server.boundPort() != 0);

  // External descriptors (eventfd here) receive their events like sockets.
  const int event_fd = ::eventfd(0, EFD_CLOEXEC);
  assert(event_fd >= 0);
  int external_hits = 0;
  assert(server.registerExternalFd(event_fd, EPOLLIN | EPOLLET,
                                   [&external_hits](std::uint32_t) { ++external_hits; }));
  const std::uint64_t wake = 1;
  assert(::write(event_fd, &wake, sizeof(wake)) == static_cast<ssize_t>(sizeof(wake)));
  pumpUntil(server, [&external_hits] { return external_hits == 1; });
  assert(external_hits == 1);
  ::close(event_fd);

  // Two participants echo independently; fragmented writes are reassembled.
  const int first = connectTo(server.boundPort());
  const int second = connectTo(server.boundPort());
  pumpUntil(server, [&server] { return server.connectionCount() == 2; });
  sendAll(first, "abcd", 4);
  sendAll(first, "efgh", 4);
  sendAll(second, "1234", 4);
  std::string first_in;
  std::string second_in;
  pumpUntil(server, [&] {
    return drainNonBlocking(first, first_in, 8) && drainNonBlocking(second, second_in, 4);
  });
  assert(first_in == "abcdefgh");
  assert(second_in == "1234");

  // A later readable edge on the same connection still echoes.
  sendAll(first, "next!!!", 7);
  first_in.clear();
  pumpUntil(server, [&] { return drainNonBlocking(first, first_in, 7); });
  assert(first_in == "next!!!");

  // An abrupt peer close is reaped and leaves the other connection healthy.
  ::close(second);
  pumpUntil(server, [&disconnects] { return disconnects == 1; });
  assert(server.connectionCount() == 1);
  sendAll(first, "alive!!", 7);
  first_in.clear();
  pumpUntil(server, [&] { return drainNonBlocking(first, first_in, 7); });
  assert(first_in == "alive!!");

  // The accept policy can turn away every connection after the first;
  // rejected sockets are closed without a disconnect notification.
  simex::transport::TCPServer guarded;
  int admissions = 0;
  int guarded_disconnects = 0;
  guarded.accept_policy_ = [&admissions](simex::transport::TCPSocket *) { return ++admissions == 1; };
  guarded.disconnect_callback_ = [&guarded_disconnects](simex::transport::TCPSocket *) { ++guarded_disconnects; };
  assert(guarded.start(0));
  const int admitted = connectTo(guarded.boundPort());
  pumpUntil(guarded, [&guarded] { return guarded.connectionCount() == 1; });
  const int rejected = connectTo(guarded.boundPort());
  pumpUntil(guarded, [&admissions] { return admissions == 2; });
  assert(guarded.connectionCount() == 1);
  assert(guarded_disconnects == 0);
  pumpUntil(guarded, [&rejected] {
    char junk[16];
    const auto count = ::recv(rejected, junk, sizeof(junk), MSG_DONTWAIT);
    return count == 0 || (count < 0 && errno == ECONNRESET);
  });

  ::close(rejected);
  ::close(admitted);
  guarded.stop();
  ::close(first);
  server.stop();
  return 0;
}
