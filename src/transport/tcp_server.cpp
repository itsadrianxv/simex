#include "transport/tcp_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace simex::transport {
TCPServer::~TCPServer() { stop(); }

auto TCPServer::start(std::uint16_t port) -> bool {
  if (epoll_fd_ >= 0) return false;
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0) return false;
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) {
    ::close(epoll_fd_);
    epoll_fd_ = -1;
    return false;
  }
  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 ||
      ::listen(listen_fd_, 64) < 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    ::close(epoll_fd_);
    epoll_fd_ = -1;
    return false;
  }
  socklen_t length = sizeof(address);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address), &length) < 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    ::close(epoll_fd_);
    epoll_fd_ = -1;
    return false;
  }
  bound_port_ = ntohs(address.sin_port);
  auto listener = std::make_unique<Registration>();
  listener->kind = Registration::Kind::kListener;
  listener->fd = listen_fd_;
  if (!addRegistration(std::move(listener), EPOLLIN | EPOLLET)) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    ::close(epoll_fd_);
    epoll_fd_ = -1;
    bound_port_ = 0;
    return false;
  }
  return true;
}

auto TCPServer::stop() -> void {
  registrations_.clear();  // TCPSocket destructors close the fds, which also removes them from epoll.
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
    epoll_fd_ = -1;
  }
  bound_port_ = 0;
  events_.clear();
}

auto TCPServer::connectionCount() const noexcept -> std::size_t {
  std::size_t count = 0;
  for (const auto &registration : registrations_)
    if (registration->kind == Registration::Kind::kConnection) ++count;
  return count;
}

auto TCPServer::addRegistration(std::unique_ptr<Registration> registration,
                                std::uint32_t events) -> bool {
  epoll_event event{};
  event.events = events;
  event.data.ptr = registration.get();
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, registration->fd, &event) < 0) return false;
  registrations_.push_back(std::move(registration));
  return true;
}

auto TCPServer::registerExternalFd(int fd, std::uint32_t events,
                                   std::function<void(std::uint32_t)> callback) -> bool {
  if (epoll_fd_ < 0) return false;
  auto registration = std::make_unique<Registration>();
  registration->kind = Registration::Kind::kExternal;
  registration->fd = fd;
  registration->external_callback = std::move(callback);
  return addRegistration(std::move(registration), events);
}

auto TCPServer::acceptPending() -> bool {
  for (;;) {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    const int fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr *>(&address), &length,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
      if (errno == EINTR) continue;
      return errno == EAGAIN || errno == EWOULDBLOCK;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    auto socket = std::make_unique<TCPSocket>(fd, recv_callback_);
    if (accept_policy_ && !accept_policy_(socket.get())) continue;  // destructor closes fd.
    auto registration = std::make_unique<Registration>();
    registration->kind = Registration::Kind::kConnection;
    registration->fd = fd;
    registration->socket = std::move(socket);
    if (!addRegistration(std::move(registration), EPOLLIN | EPOLLET)) return false;
  }
}

auto TCPServer::poll(int timeout_ms) -> bool {
  events_.resize(registrations_.size());
  const int n = ::epoll_wait(epoll_fd_, events_.data(),
                             static_cast<int>(events_.size()), timeout_ms);
  if (n < 0) return errno == EINTR;
  for (int i = 0; i < n; ++i) {
    auto *registration = static_cast<Registration *>(events_[i].data.ptr);
    switch (registration->kind) {
      case Registration::Kind::kListener:
        if ((events_[i].events & EPOLLIN) && !acceptPending()) return false;
        break;
      case Registration::Kind::kConnection:
        if (events_[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) registration->socket->setWantRecv();
        break;
      case Registration::Kind::kExternal:
        if (registration->external_callback) registration->external_callback(events_[i].events);
        break;
    }
  }
  return true;
}

auto TCPServer::sendAndRecv() -> void {
  for (auto &registration : registrations_)
    if (registration->kind == Registration::Kind::kConnection && registration->socket)
      registration->socket->sendAndRecv();
  for (std::size_t i = 0; i < registrations_.size();) {
    const auto &registration = registrations_[i];
    if (registration->kind == Registration::Kind::kConnection && registration->socket &&
        (registration->socket->dead() || registration->socket->peerClosed()))
      closeRegistration(i, true);
    else
      ++i;
  }
}

auto TCPServer::closeRegistration(std::size_t index, bool notify) -> void {
  auto &registration = registrations_[index];
  const bool connected = registration->kind == Registration::Kind::kConnection &&
                         registration->socket != nullptr;
  TCPSocket *socket = registration->socket.get();
  if (notify && connected && disconnect_callback_) disconnect_callback_(socket);
  registrations_.erase(registrations_.begin() + static_cast<std::ptrdiff_t>(index));
}

}  // namespace simex::transport
