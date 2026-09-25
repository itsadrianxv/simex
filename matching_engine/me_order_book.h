#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "exchange/messages.h"

namespace Exchange {

struct RestingOrder final {
  ClientRequest request;
  Common::MarketOrderId market_order_id = Common::INVALID_ORDER_ID;
  Common::Qty leaves_qty = 0;
  std::uint64_t arrival_index = 0;
};

struct PassiveFill final {
  RestingOrder order;
  Common::Qty fill_qty = 0;
  bool fully_filled = false;
};

struct MatchResult final {
  std::vector<PassiveFill> fills;
  Common::Qty leaves_qty = 0;
};

/// One instrument's price-time-priority resting order book.
class MEOrderBook final {
 public:
  explicit MEOrderBook(Common::InstrumentConfig config);

  auto setPriceBand(Common::PriceTicks lower_tick, Common::PriceTicks upper_tick) -> bool;
  [[nodiscard]] auto hasPriceBand() const noexcept -> bool { return has_price_band_; }

  auto add(RestingOrder order) -> bool;
  auto cancel(Common::ClientId client_id, Common::ClientOrderId client_order_id)
      -> std::optional<RestingOrder>;
  auto remove(Common::MarketOrderId market_order_id) -> std::optional<RestingOrder>;

  [[nodiscard]] auto contains(Common::ClientId client_id,
                              Common::ClientOrderId client_order_id) const noexcept -> bool;
  [[nodiscard]] auto availableQuantity(Common::Side aggressive_side,
                                       Common::OrderType order_type,
                                       Common::PriceTicks limit_price) const noexcept
      -> Common::Qty;
  auto match(Common::Side aggressive_side, Common::OrderType order_type,
             Common::PriceTicks limit_price, Common::Qty quantity) -> MatchResult;

  [[nodiscard]] auto snapshot() const -> std::vector<RestingOrder>;
  auto drain() -> std::vector<RestingOrder>;

 private:
  struct ClientKey final {
    Common::ClientId client_id = Common::INVALID_CLIENT_ID;
    Common::ClientOrderId client_order_id = Common::INVALID_ORDER_ID;
    auto operator==(const ClientKey &) const noexcept -> bool = default;
  };

  struct ClientKeyHash final {
    auto operator()(const ClientKey &key) const noexcept -> std::size_t {
      return (static_cast<std::size_t>(key.client_id) << 32U) ^
             static_cast<std::size_t>(key.client_order_id);
    }
  };

  struct PriceLevel final {
    Common::PriceTicks price = Common::INVALID_PRICE_TICKS;
    Common::Side side = Common::Side::BUY;
    std::list<RestingOrder> orders;
  };

  struct Location final {
    Common::PriceTicks price = Common::INVALID_PRICE_TICKS;
    std::list<RestingOrder>::iterator iterator;
  };

  [[nodiscard]] auto priceIndex(Common::PriceTicks price) const noexcept
      -> std::optional<std::size_t>;
  auto removeIterator(Common::PriceTicks price,
                      std::list<RestingOrder>::iterator iterator)
      -> RestingOrder;
  auto clearLevelIfEmpty(Common::PriceTicks price, Common::Side side) -> void;

  Common::InstrumentConfig config_;
  Common::PriceTicks lower_tick_ = 0;
  Common::PriceTicks upper_tick_ = 0;
  bool has_price_band_ = false;
  std::vector<PriceLevel> levels_;
  std::set<Common::PriceTicks, std::greater<Common::PriceTicks>> bid_prices_;
  std::set<Common::PriceTicks> ask_prices_;
  std::unordered_map<Common::MarketOrderId, Location> market_orders_;
  std::unordered_map<ClientKey, Common::MarketOrderId, ClientKeyHash> client_orders_;
};

}  // namespace Exchange
