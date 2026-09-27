#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "exchange/messages.h"

namespace simex::tui {

enum class ObserverState : std::uint8_t { WAITING_SNAPSHOT, SYNCED, GAP, DISCONNECTED };

struct ObservedOrder final {
  simex::exchange::MarketUpdate update{};
  std::uint64_t sequence = 0;
};

struct Event final {
  simex::exchange::MarketUpdate update{};
  std::uint64_t sequence = 0;
};

struct DepthLevel final {
  simex::common::PriceTicks price = simex::common::INVALID_PRICE_TICKS;
  std::uint64_t order_count = 0;
  std::uint64_t quantity = 0;
};

struct ObserverView final {
  ObserverState state = ObserverState::WAITING_SNAPSHOT;
  std::uint64_t sequence = 0;
  std::uint32_t ticker_id = 0;
  std::vector<DepthLevel> bids;
  std::vector<DepthLevel> asks;
  std::deque<Event> events;
  bool has_trade = false;
  simex::common::PriceTicks last_trade_price = simex::common::INVALID_PRICE_TICKS;
  simex::common::Qty last_trade_qty = 0;
};

class MarketObserver final {
 public:
  explicit MarketObserver(std::uint16_t port);
  ~MarketObserver();

  MarketObserver(const MarketObserver &) = delete;
  auto operator=(const MarketObserver &) -> MarketObserver & = delete;

  auto start() -> bool;
  auto stop() -> void;
  [[nodiscard]] auto view() const -> ObserverView;

 private:
  void run();
  auto handlePacket(const std::uint8_t *bytes, std::size_t size) -> void;
  auto handleUpdate(const simex::exchange::MarketUpdate &update, std::uint64_t sequence) -> void;
  auto handleSnapshot(const std::uint8_t *bytes, std::size_t size, std::uint64_t sequence) -> void;
  auto rebuildDepth(ObserverView *view) const -> void;

  std::uint16_t port_ = 0;
  int socket_ = -1;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
  ObserverView view_;
  std::unordered_map<simex::common::MarketOrderId, ObservedOrder> orders_;
};

}  // namespace simex::tui
