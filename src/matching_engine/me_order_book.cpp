#include "matching_engine/me_order_book.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace simex::exchange {

MEOrderBook::MEOrderBook(simex::common::InstrumentConfig config)
    : config_(std::move(config)), levels_(config_.max_price_levels) {}

auto MEOrderBook::setPriceBand(simex::common::PriceTicks lower_tick,
                               simex::common::PriceTicks upper_tick) -> bool {
  if (!market_orders_.empty() || lower_tick > upper_tick ||
      upper_tick - lower_tick + 1 > static_cast<simex::common::PriceTicks>(levels_.size())) {
    return false;
  }
  lower_tick_ = lower_tick;
  upper_tick_ = upper_tick;
  has_price_band_ = true;
  return true;
}

auto MEOrderBook::priceIndex(simex::common::PriceTicks price) const noexcept
    -> std::optional<std::size_t> {
  if (!has_price_band_ || price < lower_tick_ || price > upper_tick_) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(price - lower_tick_);
}

auto MEOrderBook::add(RestingOrder order) -> bool {
  const auto index = priceIndex(order.request.price_ticks);
  if (!index || order.leaves_qty == 0 || order.market_order_id == simex::common::INVALID_ORDER_ID) {
    return false;
  }
  const ClientKey client_key{order.request.client_id, order.request.client_order_id};
  if (market_orders_.contains(order.market_order_id) || client_orders_.contains(client_key)) {
    return false;
  }

  auto &level = levels_[*index];
  if (level.orders.empty()) {
    level.price = order.request.price_ticks;
    level.side = order.request.side;
    if (order.request.side == simex::common::Side::BUY) {
      bid_prices_.insert(order.request.price_ticks);
    } else {
      ask_prices_.insert(order.request.price_ticks);
    }
  } else if (level.side != order.request.side) {
    return false;
  }

  auto insert_before = level.orders.end();
  for (auto iterator = level.orders.begin(); iterator != level.orders.end(); ++iterator) {
    if (order.request.rx_time < iterator->request.rx_time ||
        (order.request.rx_time == iterator->request.rx_time &&
         order.arrival_index < iterator->arrival_index)) {
      insert_before = iterator;
      break;
    }
  }
  auto iterator = level.orders.insert(insert_before, std::move(order));
  const auto market_order_id = iterator->market_order_id;
  market_orders_.emplace(market_order_id, Location{iterator->request.price_ticks, iterator});
  client_orders_.emplace(client_key, market_order_id);
  return true;
}

auto MEOrderBook::clearLevelIfEmpty(simex::common::PriceTicks price, simex::common::Side side) -> void {
  const auto index = priceIndex(price);
  if (!index || !levels_[*index].orders.empty()) return;
  levels_[*index].price = simex::common::INVALID_PRICE_TICKS;
  if (side == simex::common::Side::BUY) {
    bid_prices_.erase(price);
  } else {
    ask_prices_.erase(price);
  }
}

auto MEOrderBook::removeIterator(simex::common::PriceTicks price,
                                 std::list<RestingOrder>::iterator iterator)
    -> RestingOrder {
  auto &level = levels_[*priceIndex(price)];
  const auto client_key = ClientKey{iterator->request.client_id, iterator->request.client_order_id};
  const auto market_order_id = iterator->market_order_id;
  RestingOrder result = *iterator;
  level.orders.erase(iterator);
  market_orders_.erase(market_order_id);
  client_orders_.erase(client_key);
  clearLevelIfEmpty(price, result.request.side);
  return result;
}

auto MEOrderBook::remove(simex::common::MarketOrderId market_order_id)
    -> std::optional<RestingOrder> {
  const auto found = market_orders_.find(market_order_id);
  if (found == market_orders_.end()) return std::nullopt;
  return removeIterator(found->second.price, found->second.iterator);
}

auto MEOrderBook::cancel(simex::common::ClientId client_id,
                         simex::common::ClientOrderId client_order_id)
    -> std::optional<RestingOrder> {
  const auto found = client_orders_.find(ClientKey{client_id, client_order_id});
  if (found == client_orders_.end()) return std::nullopt;
  return remove(found->second);
}

auto MEOrderBook::contains(simex::common::ClientId client_id,
                           simex::common::ClientOrderId client_order_id) const noexcept -> bool {
  return client_orders_.contains(ClientKey{client_id, client_order_id});
}

auto MEOrderBook::availableQuantity(simex::common::Side aggressive_side,
                                    simex::common::OrderType order_type,
                                    simex::common::PriceTicks limit_price) const noexcept -> simex::common::Qty {
  simex::common::Qty quantity = 0;
  const auto crosses = [&](simex::common::PriceTicks passive_price) {
    if (order_type == simex::common::OrderType::MARKET) return true;
    return aggressive_side == simex::common::Side::BUY ? passive_price <= limit_price
                                                 : passive_price >= limit_price;
  };

  if (aggressive_side == simex::common::Side::BUY) {
    for (const auto price : ask_prices_) {
      if (!crosses(price)) break;
      const auto &level = levels_[*priceIndex(price)];
      for (const auto &order : level.orders) {
        if (std::numeric_limits<simex::common::Qty>::max() - quantity < order.leaves_qty) {
          return std::numeric_limits<simex::common::Qty>::max();
        }
        quantity += order.leaves_qty;
      }
    }
  } else {
    for (const auto price : bid_prices_) {
      if (!crosses(price)) break;
      const auto &level = levels_[*priceIndex(price)];
      for (const auto &order : level.orders) {
        if (std::numeric_limits<simex::common::Qty>::max() - quantity < order.leaves_qty) {
          return std::numeric_limits<simex::common::Qty>::max();
        }
        quantity += order.leaves_qty;
      }
    }
  }
  return quantity;
}

auto MEOrderBook::match(simex::common::Side aggressive_side, simex::common::OrderType order_type,
                        simex::common::PriceTicks limit_price, simex::common::Qty quantity) -> MatchResult {
  MatchResult result;
  result.leaves_qty = quantity;
  const auto crosses = [&](simex::common::PriceTicks passive_price) {
    if (order_type == simex::common::OrderType::MARKET) return true;
    return aggressive_side == simex::common::Side::BUY ? passive_price <= limit_price
                                                 : passive_price >= limit_price;
  };

  while (result.leaves_qty > 0) {
    simex::common::PriceTicks passive_price = simex::common::INVALID_PRICE_TICKS;
    if (aggressive_side == simex::common::Side::BUY) {
      if (ask_prices_.empty()) break;
      passive_price = *ask_prices_.begin();
    } else {
      if (bid_prices_.empty()) break;
      passive_price = *bid_prices_.begin();
    }
    if (!crosses(passive_price)) break;

    auto &level = levels_[*priceIndex(passive_price)];
    auto iterator = level.orders.begin();
    const auto fill_qty = std::min(result.leaves_qty, iterator->leaves_qty);
    PassiveFill fill{*iterator, fill_qty, fill_qty == iterator->leaves_qty};
    result.leaves_qty -= fill_qty;

    iterator->leaves_qty -= fill_qty;
    if (iterator->leaves_qty == 0) {
      removeIterator(passive_price, iterator);
    }
    result.fills.push_back(std::move(fill));
  }
  return result;
}

auto MEOrderBook::snapshot() const -> std::vector<RestingOrder> {
  std::vector<RestingOrder> result;
  for (const auto price : bid_prices_) {
    const auto &level = levels_[*priceIndex(price)];
    result.insert(result.end(), level.orders.begin(), level.orders.end());
  }
  for (const auto price : ask_prices_) {
    const auto &level = levels_[*priceIndex(price)];
    result.insert(result.end(), level.orders.begin(), level.orders.end());
  }
  return result;
}

auto MEOrderBook::drain() -> std::vector<RestingOrder> {
  auto result = snapshot();
  levels_.assign(config_.max_price_levels, PriceLevel{});
  bid_prices_.clear();
  ask_prices_.clear();
  market_orders_.clear();
  client_orders_.clear();
  return result;
}

}  // namespace simex::exchange
