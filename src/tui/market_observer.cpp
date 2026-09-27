#include "tui/market_observer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "transport/protocol.h"

namespace simex::tui {

MarketObserver::MarketObserver(std::uint16_t port) : port_(port) {}

MarketObserver::~MarketObserver() { stop(); }

auto MarketObserver::start() -> bool {
  if (thread_.joinable()) return false;
  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ < 0) return false;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port_);
  if (::bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    ::close(socket_);
    socket_ = -1;
    return false;
  }
  stopping_.store(false);
  thread_ = std::thread([this] { run(); });
  return true;
}

auto MarketObserver::stop() -> void {
  stopping_.store(true);
  if (thread_.joinable()) thread_.join();
  if (socket_ >= 0) {
    ::close(socket_);
    socket_ = -1;
  }
  std::lock_guard lock(mutex_);
  view_.state = ObserverState::DISCONNECTED;
}

auto MarketObserver::view() const -> ObserverView {
  std::lock_guard lock(mutex_);
  ObserverView copy = view_;
  rebuildDepth(&copy);
  return copy;
}

auto MarketObserver::run() -> void {
  std::array<std::uint8_t, 65507> buffer{};
  pollfd descriptor{socket_, POLLIN, 0};
  auto last_packet = std::chrono::steady_clock::now();
  while (!stopping_.load()) {
    const auto ready = ::poll(&descriptor, 1, 100);
    if (ready < 0) break;
    if (ready == 0) {
      if (std::chrono::steady_clock::now() - last_packet > std::chrono::seconds(3)) {
        std::lock_guard lock(mutex_);
        view_.state = ObserverState::DISCONNECTED;
        break;
      }
      continue;
    }
    if ((descriptor.revents & POLLIN) == 0) break;
    const auto size = ::recv(socket_, buffer.data(), buffer.size(), 0);
    if (size > 0) {
      last_packet = std::chrono::steady_clock::now();
      handlePacket(buffer.data(), static_cast<std::size_t>(size));
    }
  }
}

auto MarketObserver::handlePacket(const std::uint8_t *bytes, std::size_t size) -> void {
  if (size < sizeof(simex::transport::FrameHeader)) return;
  simex::transport::FrameHeader header{};
  std::memcpy(&header, bytes, sizeof(header));
  if (header.version != simex::transport::kProtocolVersion || header.reserved != 0 ||
      header.payload_size != size - sizeof(header)) {
    return;
  }
  const auto kind = static_cast<simex::transport::MessageKind>(header.kind);
  if (kind == simex::transport::MessageKind::UPDATE && size == sizeof(simex::transport::WireUpdate)) {
    simex::exchange::MarketUpdate update{};
    std::memcpy(&update, bytes + sizeof(header), sizeof(update));
    handleUpdate(update, header.sequence);
  } else if (kind == simex::transport::MessageKind::SNAPSHOT && size >= sizeof(simex::transport::WireSnapshotPrefix)) {
    handleSnapshot(bytes, size, header.sequence);
  }
}

auto MarketObserver::handleSnapshot(const std::uint8_t *bytes, std::size_t size,
                                    std::uint64_t sequence) -> void {
  simex::transport::WireSnapshotPrefix prefix{};
  std::memcpy(&prefix, bytes, sizeof(prefix));
  const auto expected = sizeof(prefix) + static_cast<std::size_t>(prefix.order_count) * sizeof(simex::transport::WireSnapshotOrder);
  if (expected != size) return;
  std::lock_guard lock(mutex_);
  if (view_.state != ObserverState::WAITING_SNAPSHOT && sequence < view_.sequence) return;
  orders_.clear();
  const auto *orders = bytes + sizeof(prefix);
  for (std::uint32_t index = 0; index < prefix.order_count; ++index) {
    simex::transport::WireSnapshotOrder wire{};
    std::memcpy(&wire, orders + index * sizeof(wire), sizeof(wire));
    orders_[wire.update.market_order_id] = {wire.update, wire.sequence};
  }
  view_.state = ObserverState::SYNCED;
  view_.sequence = sequence;
}

auto MarketObserver::handleUpdate(const simex::exchange::MarketUpdate &update,
                                  std::uint64_t sequence) -> void {
  std::lock_guard lock(mutex_);
  if (view_.state != ObserverState::SYNCED || sequence != view_.sequence + 1) {
    if (view_.state == ObserverState::SYNCED && sequence != view_.sequence + 1) {
      view_.state = ObserverState::GAP;
    }
    return;
  }
  view_.sequence = sequence;
  view_.events.push_back({update, sequence});
  while (view_.events.size() > 20) view_.events.pop_front();
  switch (update.type) {
    case simex::common::MarketUpdateType::ADD:
      orders_[update.market_order_id] = {update, sequence};
      break;
    case simex::common::MarketUpdateType::MODIFY: {
      const auto found = orders_.find(update.market_order_id);
      if (found != orders_.end()) found->second = {update, sequence};
      break;
    }
    case simex::common::MarketUpdateType::CANCEL:
      orders_.erase(update.market_order_id);
      break;
    case simex::common::MarketUpdateType::TRADE:
      view_.has_trade = true;
      view_.last_trade_price = update.price_ticks;
      view_.last_trade_qty = update.qty;
      break;
  }
}

auto MarketObserver::rebuildDepth(ObserverView *view) const -> void {
  view->bids.clear();
  view->asks.clear();
  auto add = [](std::vector<DepthLevel> *levels, const ObservedOrder &order) {
    const auto price = order.update.price_ticks;
    auto found = std::find_if(levels->begin(), levels->end(), [price](const auto &level) {
      return level.price == price;
    });
    if (found == levels->end()) levels->push_back({price, 1, order.update.leaves_qty});
    else { ++found->order_count; found->quantity += order.update.leaves_qty; }
  };
  for (const auto &[id, order] : orders_) {
    (void)id;
    if (order.update.side == simex::common::Side::BUY) add(&view->bids, order);
    else add(&view->asks, order);
  }
  auto by_price = [](const auto &left, const auto &right) { return left.price > right.price; };
  std::sort(view->bids.begin(), view->bids.end(), by_price);
  std::sort(view->asks.begin(), view->asks.end(), [](const auto &left, const auto &right) { return left.price < right.price; });
}

}  // namespace simex::tui
