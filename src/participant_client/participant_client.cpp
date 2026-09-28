#include "participant_client/participant_client.h"

#include "transport/protocol.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace simex::participant {
namespace {

auto sendAll(int socket_fd, const void *data, std::size_t size) -> bool {
  const auto *bytes = static_cast<const char *>(data);
  std::size_t sent = 0;
  while (sent < size) {
    const auto count = ::send(socket_fd, bytes + sent, size - sent, MSG_NOSIGNAL);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

auto connectSocket(const Endpoint &endpoint, int type) -> int {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = type;
  addrinfo *results = nullptr;
  const auto port = std::to_string(endpoint.port);
  if (::getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &results) != 0)
    return -1;
  int socket_fd = -1;
  for (auto *entry = results; entry != nullptr; entry = entry->ai_next) {
    socket_fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (socket_fd < 0) continue;
    if (type == SOCK_STREAM) {
      if (::connect(socket_fd, entry->ai_addr, entry->ai_addrlen) == 0) break;
    } else {
      if (::bind(socket_fd, entry->ai_addr, entry->ai_addrlen) == 0) break;
    }
    ::close(socket_fd);
    socket_fd = -1;
  }
  ::freeaddrinfo(results);
  return socket_fd;
}

auto validRequest(const simex::exchange::ClientRequest &request) -> bool {
  if (request.client_order_id == simex::common::INVALID_ORDER_ID) return false;
  if (request.type == simex::common::RequestType::CANCEL) return request.qty == 0;
  return request.qty > 0 && request.price_ticks != simex::common::INVALID_PRICE_TICKS &&
         request.order_type == simex::common::OrderType::LIMIT &&
         request.time_in_force == simex::common::TimeInForce::DAY &&
         request.position_effect == simex::common::PositionEffect::OPEN;
}

}  // namespace

auto RequestBuilder::limit(simex::common::ClientOrderId order_id,
                           simex::common::Side side,
                           simex::common::PriceTicks price_ticks,
                           simex::common::Qty qty) -> RequestBuilder {
  RequestBuilder builder;
  builder.request_.type = simex::common::RequestType::NEW;
  builder.request_.client_order_id = order_id;
  builder.request_.side = side;
  builder.request_.price_ticks = price_ticks;
  builder.request_.qty = qty;
  return builder;
}

auto RequestBuilder::cancel(simex::common::ClientOrderId order_id) -> RequestBuilder {
  RequestBuilder builder;
  builder.request_.type = simex::common::RequestType::CANCEL;
  builder.request_.client_order_id = order_id;
  return builder;
}

auto RequestBuilder::timeInForce(simex::common::TimeInForce value) -> RequestBuilder & {
  request_.time_in_force = value;
  return *this;
}

auto RequestBuilder::positionEffect(simex::common::PositionEffect value) -> RequestBuilder & {
  request_.position_effect = value;
  return *this;
}

class ParticipantClient::Impl final {
 public:
  explicit Impl(ClientConfig config) : config_(std::move(config)) {}
  ~Impl() { close(); }

  auto connect() -> bool {
    std::lock_guard lock(mutex_);
    if (thread_.joinable()) return false;
    tcp_fd_ = connectSocket(config_.tcp, SOCK_STREAM);
    udp_fd_ = connectSocket(config_.udp, SOCK_DGRAM);
    if (tcp_fd_ < 0 || udp_fd_ < 0) {
      if (tcp_fd_ >= 0) ::close(tcp_fd_);
      if (udp_fd_ >= 0) ::close(udp_fd_);
      tcp_fd_ = udp_fd_ = -1;
      return false;
    }
    stopping_ = false;
    connected_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
  }

  auto close() -> void {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      connected_ = false;
    }
    condition_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (tcp_fd_ >= 0) ::close(tcp_fd_);
    if (udp_fd_ >= 0) ::close(udp_fd_);
    tcp_fd_ = udp_fd_ = -1;
    std::lock_guard lock(mutex_);
    requests_.clear();
    events_.clear();
    next_request_sequence_ = 0;
    next_market_sequence_ = 0;
  }

  [[nodiscard]] auto connected() const noexcept -> bool {
    std::lock_guard lock(mutex_);
    return connected_;
  }

  auto submit(const RequestBuilder &builder) -> SubmitResult {
    std::lock_guard lock(mutex_);
    if (!connected_) return {false, SubmitError::NOT_CONNECTED};
    auto request = builder.request_;
    request.client_id = config_.client_id;
    request.ticker_id = config_.ticker_id;
    if (!validRequest(request)) return {false, SubmitError::INVALID_REQUEST};
    if (requests_.size() >= config_.queue_capacity) return {false, SubmitError::QUEUE_FULL};
    requests_.push_back(request);
    condition_.notify_all();
    return {true, SubmitError::NONE};
  }

  auto poll() -> std::optional<Event> {
    std::lock_guard lock(mutex_);
    if (events_.empty()) return std::nullopt;
    auto event = std::move(events_.front());
    events_.pop_front();
    return event;
  }

  auto wait(std::chrono::milliseconds timeout) -> std::optional<Event> {
    std::unique_lock lock(mutex_);
    condition_.wait_for(lock, timeout, [this] { return !events_.empty() || stopping_; });
    if (events_.empty()) return std::nullopt;
    auto event = std::move(events_.front());
    events_.pop_front();
    return event;
  }

 private:
  void run() {
    while (true) {
      {
        std::unique_lock lock(mutex_);
        if (stopping_) break;
        if (!requests_.empty()) {
          auto request = requests_.front();
          requests_.pop_front();
          lock.unlock();
          simex::transport::WireRequest wire{};
          wire.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::REQUEST);
          wire.header.payload_size = sizeof(wire.request);
          wire.header.sequence = next_request_sequence_++;
          wire.request = request;
          if (!sendAll(tcp_fd_, &wire, sizeof(wire))) {
            std::lock_guard failed_lock(mutex_);
            connected_ = false;
            break;
          }
        }
      }
      pollfd descriptors[2]{{tcp_fd_, POLLIN, 0}, {udp_fd_, POLLIN, 0}};
      const auto result = ::poll(descriptors, 2, 5);
      if (result < 0 && errno != EINTR) break;
      if (result <= 0) continue;
      if (descriptors[0].revents & POLLIN) readTcp();
      if (descriptors[1].revents & POLLIN) readUdp();
      if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
    }
    std::lock_guard lock(mutex_);
    connected_ = false;
    stopping_ = true;
    requests_.clear();
    events_.clear();
    condition_.notify_all();
  }

  void readTcp() {
    const auto count = ::recv(tcp_fd_, tcp_buffer_.data() + tcp_buffer_size_,
                              tcp_buffer_.size() - tcp_buffer_size_, MSG_DONTWAIT);
    if (count <= 0) return;
    tcp_buffer_size_ += static_cast<std::size_t>(count);
    if (tcp_buffer_size_ < tcp_buffer_.size()) return;
    simex::transport::WireResponse wire{};
    std::memcpy(&wire, tcp_buffer_.data(), sizeof(wire));
    tcp_buffer_size_ = 0;
    if (wire.header.version != simex::transport::kProtocolVersion ||
        wire.header.kind != static_cast<std::uint8_t>(simex::transport::MessageKind::RESPONSE) ||
        wire.header.payload_size != sizeof(wire.response)) return;
    pushEvent(wire.response);
  }

  void readUdp() {
    std::array<std::byte, 65507> buffer{};
    const auto count = ::recv(udp_fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
    if (count < static_cast<ssize_t>(sizeof(simex::transport::FrameHeader))) return;
    simex::transport::FrameHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    if (header.version != simex::transport::kProtocolVersion) return;
    if (header.kind == static_cast<std::uint8_t>(simex::transport::MessageKind::UPDATE) &&
        count == static_cast<ssize_t>(sizeof(simex::transport::WireUpdate))) {
      simex::transport::WireUpdate wire{};
      std::memcpy(&wire, buffer.data(), sizeof(wire));
      if (next_market_sequence_ != 0 && wire.header.sequence != next_market_sequence_) {
        next_market_sequence_ = wire.header.sequence;
      }
      next_market_sequence_ = wire.header.sequence + 1;
      pushEvent(wire.update);
      return;
    }
    if (header.kind != static_cast<std::uint8_t>(simex::transport::MessageKind::SNAPSHOT) ||
        count < static_cast<ssize_t>(sizeof(simex::transport::WireSnapshotPrefix))) return;
    simex::transport::WireSnapshotPrefix prefix{};
    std::memcpy(&prefix, buffer.data(), sizeof(prefix));
    const auto expected = sizeof(prefix) + static_cast<std::size_t>(prefix.order_count) * sizeof(simex::transport::WireSnapshotOrder);
    if (expected != static_cast<std::size_t>(count)) return;
    simex::exchange::Snapshot snapshot;
    snapshot.last_incremental_sequence = prefix.header.sequence;
    snapshot.orders.reserve(prefix.order_count);
    for (std::uint32_t index = 0; index < prefix.order_count; ++index) {
      simex::transport::WireSnapshotOrder order{};
      std::memcpy(&order, buffer.data() + sizeof(prefix) + index * sizeof(order), sizeof(order));
      snapshot.orders.push_back({order.sequence, order.update});
    }
    next_market_sequence_ = prefix.header.sequence + 1;
    pushEvent(std::move(snapshot));
  }

  template <typename T>
  void pushEvent(T event) {
    std::lock_guard lock(mutex_);
    if (events_.size() < config_.queue_capacity) events_.emplace_back(std::move(event));
    condition_.notify_all();
  }

  ClientConfig config_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<simex::exchange::ClientRequest> requests_;
  std::deque<Event> events_;
  std::thread thread_;
  int tcp_fd_ = -1;
  int udp_fd_ = -1;
  bool stopping_ = false;
  bool connected_ = false;
  std::uint64_t next_request_sequence_ = 0;
  std::uint64_t next_market_sequence_ = 0;
  std::array<std::byte, sizeof(simex::transport::WireResponse)> tcp_buffer_{};
  std::size_t tcp_buffer_size_ = 0;
};

ParticipantClient::ParticipantClient(ClientConfig config) : impl_(new Impl(std::move(config))) {}
ParticipantClient::~ParticipantClient() { delete impl_; }
auto ParticipantClient::connect() -> bool { return impl_->connect(); }
auto ParticipantClient::close() -> void { impl_->close(); }
auto ParticipantClient::connected() const noexcept -> bool { return impl_->connected(); }
auto ParticipantClient::submit(const RequestBuilder &request) -> SubmitResult { return impl_->submit(request); }
auto ParticipantClient::poll() -> std::optional<Event> { return impl_->poll(); }
auto ParticipantClient::wait(std::chrono::milliseconds timeout) -> std::optional<Event> { return impl_->wait(timeout); }

}  // namespace simex::participant
