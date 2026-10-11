#include "matching_engine/me_order_book.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace simex::exchange {

MEOrderBook::MEOrderBook(simex::common::InstrumentConfig config)
    : config_(std::move(config)),
      orders_pool_(config_.max_open_orders),
      levels_(config_.max_price_levels) {}

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
  if (level.head == nullptr) {
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

  // TODO(hft): O(n) time-priority insertion scan per add; replace with
  // append-at-tail plus arrival_index resolution at match time.
  auto *insert_before = static_cast<OrderNode *>(nullptr);
  for (auto *iterator = level.head; iterator != nullptr; iterator = iterator->next) {
    if (order.request.rx_time < iterator->order.request.rx_time ||
        (order.request.rx_time == iterator->order.request.rx_time &&
         order.arrival_index < iterator->order.arrival_index)) {
      insert_before = iterator;
      break;
    }
  }

  auto *node = orders_pool_.allocate(order);
  if (insert_before == nullptr) {
    node->prev = level.tail;
    node->next = nullptr;
    if (level.tail != nullptr) {
      level.tail->next = node;
    } else {
      level.head = node;
    }
    level.tail = node;
  } else {
    node->prev = insert_before->prev;
    node->next = insert_before;
    if (insert_before->prev != nullptr) {
      insert_before->prev->next = node;
    } else {
      level.head = node;
    }
    insert_before->prev = node;
  }

  market_orders_.emplace(order.market_order_id, Location{order.request.price_ticks, node});
  client_orders_.emplace(client_key, order.market_order_id);
  return true;
}

auto MEOrderBook::clearLevelIfEmpty(simex::common::PriceTicks price, simex::common::Side side) -> void {
  const auto index = priceIndex(price);
  if (!index || levels_[*index].head != nullptr) return;
  levels_[*index].price = simex::common::INVALID_PRICE_TICKS;
  if (side == simex::common::Side::BUY) {
    bid_prices_.erase(price);
  } else {
    ask_prices_.erase(price);
  }
}

auto MEOrderBook::removeNode(simex::common::PriceTicks price, OrderNode *node) -> RestingOrder {
  auto &level = levels_[*priceIndex(price)];
  const auto client_key = ClientKey{node->order.request.client_id, node->order.request.client_order_id};
  const auto market_order_id = node->order.market_order_id;
  RestingOrder result = node->order;
  if (node->prev != nullptr) {
    node->prev->next = node->next;
  } else {
    level.head = node->next;
  }
  if (node->next != nullptr) {
    node->next->prev = node->prev;
  } else {
    level.tail = node->prev;
  }
  orders_pool_.deallocate(node);
  market_orders_.erase(market_order_id);
  client_orders_.erase(client_key);
  clearLevelIfEmpty(price, result.request.side);
  return result;
}

auto MEOrderBook::remove(simex::common::MarketOrderId market_order_id)
    -> std::optional<RestingOrder> {
  const auto found = market_orders_.find(market_order_id);
  if (found == market_orders_.end()) return std::nullopt;
  return removeNode(found->second.price, found->second.node);
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
      for (auto *node = level.head; node != nullptr; node = node->next) {
        if (std::numeric_limits<simex::common::Qty>::max() - quantity < node->order.leaves_qty) {
          return std::numeric_limits<simex::common::Qty>::max();
        }
        quantity += node->order.leaves_qty;
      }
    }
  } else {
    for (const auto price : bid_prices_) {
      if (!crosses(price)) break;
      const auto &level = levels_[*priceIndex(price)];
      for (auto *node = level.head; node != nullptr; node = node->next) {
        if (std::numeric_limits<simex::common::Qty>::max() - quantity < node->order.leaves_qty) {
          return std::numeric_limits<simex::common::Qty>::max();
        }
        quantity += node->order.leaves_qty;
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
    auto *node = level.head;
    const auto fill_qty = std::min(result.leaves_qty, node->order.leaves_qty);
    PassiveFill fill{node->order, fill_qty, fill_qty == node->order.leaves_qty};
    result.leaves_qty -= fill_qty;

    node->order.leaves_qty -= fill_qty;
    if (node->order.leaves_qty == 0) {
      removeNode(passive_price, node);
    }
    result.fills.push_back(std::move(fill));
  }
  return result;
}

auto MEOrderBook::snapshot() const -> std::vector<RestingOrder> {
  std::vector<RestingOrder> result;
  for (const auto price : bid_prices_) {
    const auto &level = levels_[*priceIndex(price)];
    for (auto *node = level.head; node != nullptr; node = node->next) {
      result.push_back(node->order);
    }
  }
  for (const auto price : ask_prices_) {
    const auto &level = levels_[*priceIndex(price)];
    for (auto *node = level.head; node != nullptr; node = node->next) {
      result.push_back(node->order);
    }
  }
  return result;
}

auto MEOrderBook::drain() -> std::vector<RestingOrder> {
  auto result = snapshot();
  for (auto &level : levels_) {
    auto *node = level.head;
    while (node != nullptr) {
      auto *next = node->next;
      orders_pool_.deallocate(node);
      node = next;
    }
    level.head = nullptr;
    level.tail = nullptr;
  }
  levels_.assign(config_.max_price_levels, PriceLevel{});
  bid_prices_.clear();
  ask_prices_.clear();
  market_orders_.clear();
  client_orders_.clear();
  return result;
}

}  // namespace simex::exchange
