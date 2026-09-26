#include "matching_engine/matching_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>

namespace Exchange {

MatchingEngine::MatchingEngine(Common::InstrumentConfig instrument_config,
                               FrontClearing *front_clearing,
                               ClientResponseQueue *responses,
                               MarketUpdateQueue *market_updates)
    : instrument_config_(std::move(instrument_config)),
      front_clearing_(front_clearing),
      responses_(responses),
      market_updates_(market_updates),
      book_(instrument_config_) {
  if (front_clearing_ == nullptr || responses_ == nullptr || market_updates_ == nullptr) {
    throw std::invalid_argument("MatchingEngine dependencies must not be null");
  }
}

auto MatchingEngine::setPhase(Common::SessionPhase phase) -> bool {
  if (phase == Common::SessionPhase::CLOSED && phase_ != Common::SessionPhase::CLOSED) {
    if (!cancelAllAtDailyClose()) return false;
  }
  if (phase == Common::SessionPhase::AUCTION_MATCH &&
      phase_ == Common::SessionPhase::AUCTION_SUBMIT) {
    phase_ = phase;
    return runCallAuction();
  }
  phase_ = phase;
  return true;
}

auto MatchingEngine::setReferencePrice(Common::PriceTicks previous_settlement_ticks) -> bool {
  if (previous_settlement_ticks <= 0 || !std::isfinite(instrument_config_.price_limit_percent)) {
    return false;
  }
  if (!book_.setPriceBand(
          static_cast<Common::PriceTicks>(std::llround(
              static_cast<double>(previous_settlement_ticks) *
              (1.0 - instrument_config_.price_limit_percent))),
          static_cast<Common::PriceTicks>(std::llround(
              static_cast<double>(previous_settlement_ticks) *
              (1.0 + instrument_config_.price_limit_percent))))) {
    return false;
  }
  lower_price_limit_ = static_cast<Common::PriceTicks>(std::llround(
      static_cast<double>(previous_settlement_ticks) *
      (1.0 - instrument_config_.price_limit_percent)));
  upper_price_limit_ = static_cast<Common::PriceTicks>(std::llround(
      static_cast<double>(previous_settlement_ticks) *
      (1.0 + instrument_config_.price_limit_percent)));
  previous_settlement_ticks_ = previous_settlement_ticks;
  previous_trade_price_ = previous_settlement_ticks;
  return true;
}

auto MatchingEngine::onTradingDayRollover() -> bool {
  if (!cancelAllAtDailyClose() || !front_clearing_->onTradingDayRollover()) {
    return false;
  }
  phase_ = Common::SessionPhase::CLOSED;
  previous_settlement_ticks_ = Common::INVALID_PRICE_TICKS;
  previous_trade_price_ = Common::INVALID_PRICE_TICKS;
  lower_price_limit_ = Common::INVALID_PRICE_TICKS;
  upper_price_limit_ = Common::INVALID_PRICE_TICKS;
  return true;
}

auto MatchingEngine::cancelAllAtDailyClose() -> bool {
  const auto orders = book_.drain();
  for (const auto &order : orders) {
    if (!front_clearing_->onCancel(clearingOrder(order), order.leaves_qty)) {
      return false;
    }
    emitResponse({Common::ResponseType::CANCELED,
                  Common::ReasonCode::SESSION_END,
                  order.request.client_id,
                  order.request.ticker_id,
                  order.request.client_order_id,
                  order.market_order_id,
                  order.request.side,
                  order.request.position_effect,
                  order.request.price_ticks,
                  0,
                  0});
    emitMarketUpdate({Common::MarketUpdateType::CANCEL,
                      order.request.ticker_id,
                      order.market_order_id,
                      order.request.side,
                      order.request.price_ticks,
                      order.leaves_qty,
                      0,
                      order.request.rx_time});
  }
  return true;
}

auto MatchingEngine::processClientRequest(const ClientRequest &request) -> bool {
  return request.type == Common::RequestType::NEW ? processNew(request) : processCancel(request);
}

auto MatchingEngine::canonicalState() const -> std::string {
  std::ostringstream output;
  output << static_cast<unsigned>(phase_) << ':' << previous_settlement_ticks_ << ':'
         << previous_trade_price_ << ':' << lower_price_limit_ << ':' << upper_price_limit_ << '|';
  for (const auto &order : book_.snapshot()) {
    output << order.request.client_id << ':' << order.request.client_order_id << ':'
           << order.market_order_id << ':' << static_cast<int>(order.request.side) << ':'
           << order.request.price_ticks << ':' << order.leaves_qty << ':'
           << order.request.rx_time << ':' << order.arrival_index << ';';
  }
  output << "|" << front_clearing_->canonicalState();
  return output.str();
}

auto MatchingEngine::runCallAuction() -> bool {
  if (phase_ != Common::SessionPhase::AUCTION_MATCH ||
      previous_settlement_ticks_ == Common::INVALID_PRICE_TICKS) {
    return false;
  }

  auto orders = book_.drain();
  if (orders.empty()) return true;

  std::vector<Common::PriceTicks> candidates;
  candidates.reserve(orders.size());
  for (const auto &order : orders) candidates.push_back(order.request.price_ticks);
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

  auto quantityAt = [&](Common::Side side, Common::PriceTicks price) {
    std::uint64_t quantity = 0;
    for (const auto &order : orders) {
      if ((side == Common::Side::BUY && order.request.price_ticks >= price) ||
          (side == Common::Side::SELL && order.request.price_ticks <= price)) {
        quantity += order.leaves_qty;
      }
    }
    return quantity;
  };

  auto better = [&](Common::PriceTicks candidate, Common::PriceTicks incumbent) {
    const auto candidate_buy = quantityAt(Common::Side::BUY, candidate);
    const auto candidate_sell = quantityAt(Common::Side::SELL, candidate);
    const auto incumbent_buy = quantityAt(Common::Side::BUY, incumbent);
    const auto incumbent_sell = quantityAt(Common::Side::SELL, incumbent);
    const auto candidate_exec = std::min(candidate_buy, candidate_sell);
    const auto incumbent_exec = std::min(incumbent_buy, incumbent_sell);
    if (candidate_exec != incumbent_exec) return candidate_exec > incumbent_exec;

    const auto candidate_unmatched = candidate_buy + candidate_sell - 2 * candidate_exec;
    const auto incumbent_unmatched = incumbent_buy + incumbent_sell - 2 * incumbent_exec;
    if (candidate_unmatched != incumbent_unmatched) return candidate_unmatched < incumbent_unmatched;

    const auto candidate_distance = std::llabs(candidate - previous_settlement_ticks_);
    const auto incumbent_distance = std::llabs(incumbent - previous_settlement_ticks_);
    if (candidate_distance != incumbent_distance) return candidate_distance < incumbent_distance;
    return candidate < incumbent;
  };

  auto auction_price = candidates.front();
  for (const auto candidate : candidates) {
    if (better(candidate, auction_price)) auction_price = candidate;
  }

  std::vector<std::size_t> buys;
  std::vector<std::size_t> sells;
  for (std::size_t index = 0; index < orders.size(); ++index) {
    if (orders[index].request.side == Common::Side::BUY &&
        orders[index].request.price_ticks >= auction_price) {
      buys.push_back(index);
    } else if (orders[index].request.side == Common::Side::SELL &&
               orders[index].request.price_ticks <= auction_price) {
      sells.push_back(index);
    }
  }
  const auto by_buy_priority = [&](std::size_t left, std::size_t right) {
    if (orders[left].request.price_ticks != orders[right].request.price_ticks) {
      return orders[left].request.price_ticks > orders[right].request.price_ticks;
    }
    if (orders[left].request.rx_time != orders[right].request.rx_time) {
      return orders[left].request.rx_time < orders[right].request.rx_time;
    }
    return orders[left].arrival_index < orders[right].arrival_index;
  };
  const auto by_sell_priority = [&](std::size_t left, std::size_t right) {
    if (orders[left].request.price_ticks != orders[right].request.price_ticks) {
      return orders[left].request.price_ticks < orders[right].request.price_ticks;
    }
    if (orders[left].request.rx_time != orders[right].request.rx_time) {
      return orders[left].request.rx_time < orders[right].request.rx_time;
    }
    return orders[left].arrival_index < orders[right].arrival_index;
  };
  std::sort(buys.begin(), buys.end(), by_buy_priority);
  std::sort(sells.begin(), sells.end(), by_sell_priority);

  std::size_t buy_index = 0;
  std::size_t sell_index = 0;
  while (buy_index < buys.size() && sell_index < sells.size()) {
    auto &buy = orders[buys[buy_index]];
    auto &sell = orders[sells[sell_index]];
    const auto fill_qty = std::min(buy.leaves_qty, sell.leaves_qty);
    if (!front_clearing_->onFill(clearingOrder(buy), fill_qty) ||
        !front_clearing_->onFill(clearingOrder(sell), fill_qty)) {
      throw std::logic_error("front clearing rejected an auction fill");
    }
    buy.leaves_qty -= fill_qty;
    sell.leaves_qty -= fill_qty;
    emitResponse({Common::ResponseType::FILLED,
                  Common::ReasonCode::NONE,
                  buy.request.client_id,
                  buy.request.ticker_id,
                  buy.request.client_order_id,
                  buy.market_order_id,
                  buy.request.side,
                  buy.request.position_effect,
                  auction_price,
                  fill_qty,
                  buy.leaves_qty});
    emitResponse({Common::ResponseType::FILLED,
                  Common::ReasonCode::NONE,
                  sell.request.client_id,
                  sell.request.ticker_id,
                  sell.request.client_order_id,
                  sell.market_order_id,
                  sell.request.side,
                  sell.request.position_effect,
                  auction_price,
                  fill_qty,
                  sell.leaves_qty});
    emitMarketUpdate({Common::MarketUpdateType::TRADE,
                      0,
                      Common::INVALID_ORDER_ID,
                      Common::Side::BUY,
                      auction_price,
                      fill_qty,
                      0,
                      std::max(buy.request.rx_time, sell.request.rx_time)});
    if (buy.leaves_qty == 0) {
      emitMarketUpdate({Common::MarketUpdateType::CANCEL, 0, buy.market_order_id,
                        buy.request.side, buy.request.price_ticks, fill_qty, 0,
                        buy.request.rx_time});
      ++buy_index;
    } else {
      emitMarketUpdate({Common::MarketUpdateType::MODIFY, 0, buy.market_order_id,
                        buy.request.side, buy.request.price_ticks, fill_qty,
                        buy.leaves_qty, buy.request.rx_time});
    }
    if (sell.leaves_qty == 0) {
      emitMarketUpdate({Common::MarketUpdateType::CANCEL, 0, sell.market_order_id,
                        sell.request.side, sell.request.price_ticks, fill_qty, 0,
                        sell.request.rx_time});
      ++sell_index;
    } else {
      emitMarketUpdate({Common::MarketUpdateType::MODIFY, 0, sell.market_order_id,
                        sell.request.side, sell.request.price_ticks, fill_qty,
                        sell.leaves_qty, sell.request.rx_time});
    }
    previous_trade_price_ = auction_price;
  }

  for (auto &order : orders) {
    if (order.leaves_qty > 0) {
      if (!book_.add(order)) throw std::logic_error("failed to carry auction order");
    }
  }
  return true;
}

auto MatchingEngine::isSupported(const ClientRequest &request) const noexcept -> bool {
  return std::any_of(instrument_config_.supported_order_combinations.begin(),
                     instrument_config_.supported_order_combinations.end(),
                     [&request](const auto &combination) {
                       return combination.order_type == request.order_type &&
                              combination.time_in_force == request.time_in_force;
                     });
}

auto MatchingEngine::validateNew(const ClientRequest &request) const -> Common::ReasonCode {
  if (request.client_id == Common::INVALID_CLIENT_ID ||
      request.client_order_id == Common::INVALID_ORDER_ID || request.qty == 0) {
    return Common::ReasonCode::INVALID_QTY;
  }
  if (request.ticker_id != 0 || (request.side != Common::Side::BUY && request.side != Common::Side::SELL)) {
    return Common::ReasonCode::INVALID_PRICE;
  }
  if (!isSupported(request)) return Common::ReasonCode::ORDER_TYPE_NOT_ALLOWED;
  if (phase_ == Common::SessionPhase::CLOSED || phase_ == Common::SessionPhase::BREAK ||
      phase_ == Common::SessionPhase::AUCTION_MATCH) {
    return Common::ReasonCode::SESSION_CLOSED;
  }
  if (phase_ == Common::SessionPhase::AUCTION_SUBMIT &&
      ((instrument_config_.auction_allowed_order_types.empty()
            ? request.order_type != Common::OrderType::LIMIT
            : std::find(instrument_config_.auction_allowed_order_types.begin(),
                        instrument_config_.auction_allowed_order_types.end(), request.order_type) ==
                  instrument_config_.auction_allowed_order_types.end()) ||
       (instrument_config_.auction_allowed_time_in_force.empty()
            ? request.time_in_force != Common::TimeInForce::DAY
            : std::find(instrument_config_.auction_allowed_time_in_force.begin(),
                        instrument_config_.auction_allowed_time_in_force.end(), request.time_in_force) ==
                  instrument_config_.auction_allowed_time_in_force.end()))) {
    return Common::ReasonCode::ORDER_TYPE_NOT_ALLOWED;
  }
  if (request.order_type == Common::OrderType::LIMIT) {
    if (request.price_ticks == Common::INVALID_PRICE_TICKS ||
        request.price_ticks <= 0 || request.price_ticks % instrument_config_.tick_size != 0) {
      return Common::ReasonCode::INVALID_TICK;
    }
    if (previous_settlement_ticks_ == Common::INVALID_PRICE_TICKS) {
      return Common::ReasonCode::REFERENCE_PRICE_UNAVAILABLE;
    }
    if (request.price_ticks < lower_price_limit_ || request.price_ticks > upper_price_limit_) {
      return Common::ReasonCode::PRICE_LIMIT_EXCEEDED;
    }
  }
  if (book_.contains(request.client_id, request.client_order_id)) {
    return Common::ReasonCode::DUPLICATE_ORDER_ID;
  }
  return Common::ReasonCode::NONE;
}

auto MatchingEngine::processNew(const ClientRequest &request) -> bool {
  const auto reason = validateNew(request);
  if (reason != Common::ReasonCode::NONE) {
    emitResponse({Common::ResponseType::REJECTED,
                  reason,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  Common::INVALID_ORDER_ID,
                  request.side,
                  request.position_effect,
                  request.price_ticks,
                  0,
                  request.qty});
    return true;
  }

  const auto available = book_.availableQuantity(request.side, request.order_type, request.price_ticks);
  if (request.time_in_force == Common::TimeInForce::FOK && available < request.qty) {
    emitResponse({Common::ResponseType::REJECTED,
                  Common::ReasonCode::FOK_NOT_FILLED,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  Common::INVALID_ORDER_ID,
                  request.side,
                  request.position_effect,
                  request.price_ticks,
                  0,
                  request.qty});
    return true;
  }

  const auto clearing_admission = front_clearing_->validateAndReserve(clearingOrder(request));
  if (!clearing_admission.accepted) {
    emitResponse({Common::ResponseType::REJECTED,
                  clearing_admission.reason,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  Common::INVALID_ORDER_ID,
                  request.side,
                  request.position_effect,
                  request.price_ticks,
                  0,
                  request.qty});
    return true;
  }

  const auto market_order_id = next_market_order_id_++;
  emitResponse({Common::ResponseType::ACCEPTED,
                Common::ReasonCode::NONE,
                request.client_id,
                request.ticker_id,
                request.client_order_id,
                market_order_id,
                request.side,
                request.position_effect,
                request.price_ticks,
                0,
                request.qty});

  if (phase_ == Common::SessionPhase::AUCTION_SUBMIT) {
    const RestingOrder auction_order{request, market_order_id, request.qty, next_arrival_index_++};
    if (!book_.add(auction_order)) throw std::logic_error("failed to add auction order to book");
    emitMarketUpdate({Common::MarketUpdateType::ADD,
                      request.ticker_id,
                      market_order_id,
                      request.side,
                      request.price_ticks,
                      request.qty,
                      request.qty,
                      request.rx_time});
    return true;
  }

  const auto match_result = book_.match(request.side, request.order_type,
                                        request.price_ticks, request.qty);
  auto leaves_qty = request.qty;
  for (const auto &passive_fill : match_result.fills) {
    leaves_qty -= passive_fill.fill_qty;
    const auto price = tradePrice(request, passive_fill.order);
    const auto aggressive_clearing = clearingOrder(request);
    const auto passive_clearing = clearingOrder(passive_fill.order);
    if (!front_clearing_->onFill(aggressive_clearing, passive_fill.fill_qty) ||
        !front_clearing_->onFill(passive_clearing, passive_fill.fill_qty)) {
      throw std::logic_error("front clearing rejected a matched fill");
    }
    emitResponse({Common::ResponseType::FILLED,
                  Common::ReasonCode::NONE,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  market_order_id,
                  request.side,
                  request.position_effect,
                  price,
                  passive_fill.fill_qty,
                  leaves_qty});
    emitResponse({Common::ResponseType::FILLED,
                  Common::ReasonCode::NONE,
                  passive_fill.order.request.client_id,
                  passive_fill.order.request.ticker_id,
                  passive_fill.order.request.client_order_id,
                  passive_fill.order.market_order_id,
                  passive_fill.order.request.side,
                  passive_fill.order.request.position_effect,
                  price,
                  passive_fill.fill_qty,
                  passive_fill.order.leaves_qty - passive_fill.fill_qty});
    emitMarketUpdate({Common::MarketUpdateType::TRADE,
                      request.ticker_id,
                      Common::INVALID_ORDER_ID,
                      request.side,
                      price,
                      passive_fill.fill_qty,
                      0,
                      request.rx_time});
    if (passive_fill.fully_filled) {
      emitMarketUpdate({Common::MarketUpdateType::CANCEL,
                        passive_fill.order.request.ticker_id,
                        passive_fill.order.market_order_id,
                        passive_fill.order.request.side,
                        passive_fill.order.request.price_ticks,
                        passive_fill.fill_qty,
                        0,
                        passive_fill.order.request.rx_time});
    } else {
      emitMarketUpdate({Common::MarketUpdateType::MODIFY,
                        passive_fill.order.request.ticker_id,
                        passive_fill.order.market_order_id,
                        passive_fill.order.request.side,
                        passive_fill.order.request.price_ticks,
                        passive_fill.fill_qty,
                        passive_fill.order.leaves_qty - passive_fill.fill_qty,
                        passive_fill.order.request.rx_time});
    }
    previous_trade_price_ = price;
    (void)market_order_id;
  }

  if (leaves_qty > 0) {
    if (request.order_type == Common::OrderType::LIMIT &&
        request.time_in_force == Common::TimeInForce::DAY) {
      const RestingOrder resting{request, market_order_id, leaves_qty, next_arrival_index_++};
      if (!book_.add(resting)) throw std::logic_error("failed to add validated order to book");
      emitMarketUpdate({Common::MarketUpdateType::ADD,
                        request.ticker_id,
                        market_order_id,
                        request.side,
                        request.price_ticks,
                        leaves_qty,
                        leaves_qty,
                        request.rx_time});
    } else {
      if (!front_clearing_->onCancel(clearingOrder(request), leaves_qty)) {
        throw std::logic_error("front clearing failed to release canceled remainder");
      }
      emitResponse({Common::ResponseType::CANCELED,
                    Common::ReasonCode::NONE,
                    request.client_id,
                    request.ticker_id,
                    request.client_order_id,
                    market_order_id,
                    request.side,
                    request.position_effect,
                    request.price_ticks,
                    0,
                    0});
    }
  }
  return true;
}

auto MatchingEngine::processCancel(const ClientRequest &request) -> bool {
  if (phase_ != Common::SessionPhase::AUCTION_SUBMIT && phase_ != Common::SessionPhase::CONTINUOUS) {
    emitResponse({Common::ResponseType::CANCEL_REJECTED,
                  Common::ReasonCode::SESSION_CLOSED,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  Common::INVALID_ORDER_ID,
                  request.side,
                  request.position_effect,
                  Common::INVALID_PRICE_TICKS,
                  0,
                  0});
    return true;
  }
  const auto canceled = book_.cancel(request.client_id, request.client_order_id);
  if (!canceled) {
    emitResponse({Common::ResponseType::CANCEL_REJECTED,
                  Common::ReasonCode::ORDER_NOT_FOUND,
                  request.client_id,
                  request.ticker_id,
                  request.client_order_id,
                  Common::INVALID_ORDER_ID,
                  request.side,
                  request.position_effect,
                  Common::INVALID_PRICE_TICKS,
                  0,
                  0});
    return true;
  }
  if (!front_clearing_->onCancel(clearingOrder(*canceled), canceled->leaves_qty)) {
    throw std::logic_error("front clearing failed to release canceled order");
  }
  emitResponse({Common::ResponseType::CANCELED,
                Common::ReasonCode::NONE,
                canceled->request.client_id,
                canceled->request.ticker_id,
                canceled->request.client_order_id,
                canceled->market_order_id,
                canceled->request.side,
                canceled->request.position_effect,
                canceled->request.price_ticks,
                0,
                0});
  emitMarketUpdate({Common::MarketUpdateType::CANCEL,
                    canceled->request.ticker_id,
                    canceled->market_order_id,
                    canceled->request.side,
                    canceled->request.price_ticks,
                    canceled->leaves_qty,
                    0,
                    canceled->request.rx_time});
  return true;
}

auto MatchingEngine::emitResponse(ClientResponse response) -> void {
  if (!responses_->tryPush(std::move(response))) {
    throw std::logic_error("client response queue is full");
  }
}

auto MatchingEngine::emitMarketUpdate(MarketUpdate update) -> void {
  if (!market_updates_->tryPush(std::move(update))) {
    throw std::logic_error("market update queue is full");
  }
}

auto MatchingEngine::medianPrice(Common::PriceTicks first, Common::PriceTicks second,
                                 Common::PriceTicks third) const noexcept -> Common::PriceTicks {
  std::array values{first, second, third};
  std::sort(values.begin(), values.end());
  return values[1];
}

auto MatchingEngine::tradePrice(const ClientRequest &aggressor,
                                const RestingOrder &passive) const noexcept -> Common::PriceTicks {
  if (aggressor.order_type == Common::OrderType::MARKET) {
    return passive.request.price_ticks;
  }
  const auto buy_price = aggressor.side == Common::Side::BUY ? aggressor.price_ticks
                                                               : passive.request.price_ticks;
  const auto sell_price = aggressor.side == Common::Side::SELL ? aggressor.price_ticks
                                                                : passive.request.price_ticks;
  return medianPrice(buy_price, sell_price, previous_trade_price_);
}

auto MatchingEngine::clearingOrder(const ClientRequest &request) const noexcept -> ClearingOrder {
  return {request.client_id, request.client_order_id, request.side,
          request.position_effect, request.qty};
}

auto MatchingEngine::clearingOrder(const RestingOrder &order) const noexcept -> ClearingOrder {
  return clearingOrder(order.request);
}

}  // namespace Exchange
