#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "exchange/messages.h"

namespace simex::exchange {

struct RestingOrder final {
  ClientRequest request;
  simex::common::MarketOrderId market_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::Qty leaves_qty = 0;
  std::uint64_t arrival_index = 0;
};

struct PassiveFill final {
  RestingOrder order;
  simex::common::Qty fill_qty = 0;
  bool fully_filled = false;
};

struct MatchResult final {
  std::vector<PassiveFill> fills;
  simex::common::Qty leaves_qty = 0;
};

/// One instrument's price-time-priority resting order book.
class MEOrderBook final {
 public:
  explicit MEOrderBook(simex::common::InstrumentConfig config);

  auto setPriceBand(simex::common::PriceTicks lower_tick, simex::common::PriceTicks upper_tick) -> bool;
  [[nodiscard]] auto hasPriceBand() const noexcept -> bool { return has_price_band_; }

  auto add(RestingOrder order) -> bool;
  auto cancel(simex::common::ClientId client_id, simex::common::ClientOrderId client_order_id)
      -> std::optional<RestingOrder>;
  auto remove(simex::common::MarketOrderId market_order_id) -> std::optional<RestingOrder>;

  [[nodiscard]] auto contains(simex::common::ClientId client_id,
                              simex::common::ClientOrderId client_order_id) const noexcept -> bool;
  [[nodiscard]] auto availableQuantity(simex::common::Side aggressive_side,
                                       simex::common::OrderType order_type,
                                       simex::common::PriceTicks limit_price) const noexcept
      -> simex::common::Qty;
  auto match(simex::common::Side aggressive_side, simex::common::OrderType order_type,
             simex::common::PriceTicks limit_price, simex::common::Qty quantity) -> MatchResult;

  [[nodiscard]] auto snapshot() const -> std::vector<RestingOrder>;
  auto drain() -> std::vector<RestingOrder>;

 private:
  struct ClientKey final {
    simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
    simex::common::ClientOrderId client_order_id = simex::common::INVALID_ORDER_ID;
    auto operator==(const ClientKey &) const noexcept -> bool = default;
  };

  struct ClientKeyHash final {
    auto operator()(const ClientKey &key) const noexcept -> std::size_t {
      return (static_cast<std::size_t>(key.client_id) << 32U) ^
             static_cast<std::size_t>(key.client_order_id);
    }
  };

  struct PriceLevel final {
    simex::common::PriceTicks price = simex::common::INVALID_PRICE_TICKS;
    simex::common::Side side = simex::common::Side::BUY;
    std::list<RestingOrder> orders;
  };

  struct Location final {
    simex::common::PriceTicks price = simex::common::INVALID_PRICE_TICKS;
    std::list<RestingOrder>::iterator iterator;
  };

  [[nodiscard]] auto priceIndex(simex::common::PriceTicks price) const noexcept
      -> std::optional<std::size_t>;
  auto removeIterator(simex::common::PriceTicks price,
                      std::list<RestingOrder>::iterator iterator)
      -> RestingOrder;
  auto clearLevelIfEmpty(simex::common::PriceTicks price, simex::common::Side side) -> void;

  simex::common::InstrumentConfig config_;
  simex::common::PriceTicks lower_tick_ = 0;
  simex::common::PriceTicks upper_tick_ = 0;
  bool has_price_band_ = false;
  std::vector<PriceLevel> levels_;
  std::set<simex::common::PriceTicks, std::greater<simex::common::PriceTicks>> bid_prices_;
  std::set<simex::common::PriceTicks> ask_prices_;
  std::unordered_map<simex::common::MarketOrderId, Location> market_orders_;
  std::unordered_map<ClientKey, simex::common::MarketOrderId, ClientKeyHash> client_orders_;
};

}  // namespace simex::exchange
