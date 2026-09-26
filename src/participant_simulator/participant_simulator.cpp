#include "participant_simulator/participant_simulator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace simex::participant {

namespace {

constexpr auto kNanosPerSecond = simex::common::Nanos{1'000'000'000};

auto saturatingAdd(simex::common::Nanos value, simex::common::Nanos delta) noexcept
    -> simex::common::Nanos {
  if (delta > 0 && value > std::numeric_limits<simex::common::Nanos>::max() - delta) {
    return std::numeric_limits<simex::common::Nanos>::max();
  }
  if (delta < 0 && value < std::numeric_limits<simex::common::Nanos>::min() - delta) {
    return std::numeric_limits<simex::common::Nanos>::min();
  }
  return value + delta;
}

auto safeMultiply(simex::common::PriceTicks value, std::size_t multiplier) noexcept
    -> simex::common::PriceTicks {
  if (multiplier == 0 || value == 0) return 0;
  const auto max_value = std::numeric_limits<simex::common::PriceTicks>::max();
  if (value > 0 && multiplier > static_cast<std::size_t>(max_value / value)) return max_value;
  return value * static_cast<simex::common::PriceTicks>(multiplier);
}

}  // namespace

ParticipantSimulator::ParticipantSimulator(simex::common::InstrumentConfig instrument,
                                           ParticipantSimulatorConfig config,
                                           simex::common::PriceTicks reference_price)
    : instrument_(std::move(instrument)),
      config_(std::move(config)),
      random_(config_.seed),
      reference_price_(simex::common::INVALID_PRICE_TICKS),
      fair_value_ticks_(simex::common::INVALID_PRICE_TICKS) {
  validateConfiguration();
  const auto total_bots = config_.market_maker_count + config_.taker_count;
  bots_.reserve(total_bots);
  for (std::size_t index = 0; index < total_bots; ++index) {
    Bot bot;
    bot.role = index < config_.market_maker_count ? BotRole::MARKET_MAKER : BotRole::TAKER;
    bot.client_id = static_cast<simex::common::ClientId>(
        static_cast<std::uint64_t>(config_.client_id_start) + index);
    bots_.push_back(std::move(bot));
  }
  if (reference_price != simex::common::INVALID_PRICE_TICKS && !setReferencePrice(reference_price)) {
    throw std::invalid_argument("ParticipantSimulator reference price is invalid");
  }
}

auto ParticipantSimulator::validateConfiguration() const -> void {
  if (instrument_.tick_size <= 0 || instrument_.max_price_levels == 0 ||
      !std::isfinite(instrument_.price_limit_percent) || instrument_.price_limit_percent < 0.0) {
    throw std::invalid_argument("ParticipantSimulator requires a valid instrument tick and price band");
  }
  if (config_.ticker_id != 0) {
    throw std::invalid_argument("ParticipantSimulator currently supports ticker_id 0 only");
  }
  if (config_.client_id_start == simex::common::INVALID_CLIENT_ID) {
    throw std::invalid_argument("ParticipantSimulator client_id_start is invalid");
  }
  const auto total_bots = config_.market_maker_count + config_.taker_count;
  if (total_bots > static_cast<std::size_t>(std::numeric_limits<simex::common::ClientId>::max()) + 1ULL ||
      (total_bots > 0 &&
       static_cast<std::uint64_t>(config_.client_id_start) + total_bots - 1ULL >=
           static_cast<std::uint64_t>(simex::common::INVALID_CLIENT_ID))) {
    throw std::invalid_argument("ParticipantSimulator client ID range overflows");
  }
  if (config_.market_maker_count > 0 && config_.quote_levels == 0) {
    throw std::invalid_argument("ParticipantSimulator quote_levels must be positive");
  }
  if (config_.fair_value_interval_nanos <= 0 || config_.quote_ttl_nanos <= 0) {
    throw std::invalid_argument("ParticipantSimulator time intervals must be positive");
  }
  if (config_.min_qty == 0 || config_.min_qty > config_.max_qty) {
    throw std::invalid_argument("ParticipantSimulator quantity range is invalid");
  }
  if (config_.max_position == 0 && (config_.market_maker_count > 0 || config_.taker_count > 0)) {
    throw std::invalid_argument("ParticipantSimulator max_position must be positive");
  }
  if (config_.quote_spread_ticks < instrument_.tick_size) {
    throw std::invalid_argument("ParticipantSimulator quote_spread_ticks is below one tick");
  }
}

auto ParticipantSimulator::setReferencePrice(simex::common::PriceTicks reference_price) -> bool {
  if (reference_price <= 0 || instrument_.tick_size <= 0 ||
      !std::isfinite(instrument_.price_limit_percent) || instrument_.price_limit_percent < 0.0) {
    return false;
  }
  const auto lower = static_cast<simex::common::PriceTicks>(std::llround(
      static_cast<double>(reference_price) * (1.0 - instrument_.price_limit_percent)));
  const auto upper = static_cast<simex::common::PriceTicks>(std::llround(
      static_cast<double>(reference_price) * (1.0 + instrument_.price_limit_percent)));
  if (lower <= 0 || upper < lower ||
      upper - lower + 1 > static_cast<simex::common::PriceTicks>(instrument_.max_price_levels)) {
    return false;
  }
  reference_price_ = reference_price;
  lower_price_limit_ = lower;
  upper_price_limit_ = upper;
  const auto requested_fair = config_.fair_value_ticks == simex::common::INVALID_PRICE_TICKS
                                  ? reference_price
                                  : config_.fair_value_ticks;
  fair_value_ticks_ = clampPrice(requested_fair);
  if (fair_value_ticks_ == simex::common::INVALID_PRICE_TICKS) return false;
  fair_value_initialized_ = true;
  last_fair_value_update_ = 0;
  return true;
}

auto ParticipantSimulator::tick(simex::common::Nanos now,
                                 simex::common::SessionPhase phase) -> RequestList {
  RequestList requests;
  if (!config_.enabled || reference_price_ == simex::common::INVALID_PRICE_TICKS ||
      !fair_value_initialized_ || config_.orders_per_second == 0) {
    return requests;
  }
  if (phase != simex::common::SessionPhase::CONTINUOUS) {
    next_request_time_ = now;
    reseed_quotes_ = true;
    return requests;
  }

  updateFairValue(now);
  const auto first_batch = stats_.generated_requests == 0 || reseed_quotes_;
  if (!first_batch && now < next_request_time_) return requests;

  // The first continuous tick seeds all configured maker levels so that a
  // fresh service can expose both sides.  Afterwards the same simple global
  // interval gates every request (maker refreshes, cancels, and takers).
  scheduleExpiredMakerCancels(now, requests);
  if (first_batch || requests.empty()) generateMakerRequests(now, requests);
  if (requests.empty()) generateTakerRequest(now, requests);

  if (!requests.empty()) {
    const auto interval = std::max<simex::common::Nanos>(
        1, kNanosPerSecond / static_cast<simex::common::Nanos>(config_.orders_per_second));
    next_request_time_ = saturatingAdd(now, interval);
    reseed_quotes_ = false;
  }
  return requests;
}

auto ParticipantSimulator::updateFairValue(simex::common::Nanos now) -> void {
  if (!fair_value_initialized_) return;
  if (last_fair_value_update_ != 0 && now < saturatingAdd(last_fair_value_update_,
                                                           config_.fair_value_interval_nanos)) {
    return;
  }
  if (last_fair_value_update_ == 0) {
    last_fair_value_update_ = now;
    return;
  }

  const auto step = std::max<simex::common::PriceTicks>(0, config_.fair_value_step_ticks);
  std::uniform_int_distribution<simex::common::PriceTicks> noise(-step, step);
  auto delta = noise(random_);
  if (config_.fair_value_reversion_ticks > 0 && fair_value_ticks_ != reference_price_) {
    const auto distance = static_cast<simex::common::PriceTicks>(
        std::llabs(reference_price_ - fair_value_ticks_));
    const auto reversion = std::min(config_.fair_value_reversion_ticks, distance);
    delta += fair_value_ticks_ < reference_price_ ? reversion : -reversion;
  }
  fair_value_ticks_ = clampPrice(fair_value_ticks_ + delta);
  last_fair_value_update_ = now;
}

auto ParticipantSimulator::scheduleExpiredMakerCancels(simex::common::Nanos now,
                                                        RequestList &requests) -> void {
  const auto first_batch = stats_.generated_requests == 0;
  for (auto &bot : bots_) {
    for (auto &order : bot.orders) {
      if (order.state != OrderState::LIVE) continue;
      if (now < saturatingAdd(order.submitted_at, config_.quote_ttl_nanos)) continue;
      requests.push_back(makeCancelRequest(bot, order, now));
      ++stats_.generated_requests;
      ++stats_.generated_cancel_requests;
      if (!first_batch) return;
    }
  }
}

auto ParticipantSimulator::generateMakerRequests(simex::common::Nanos now,
                                                 RequestList &requests) -> void {
  const auto first_batch = stats_.generated_requests == 0 || reseed_quotes_;
  for (auto &bot : bots_) {
    if (bot.role != BotRole::MARKET_MAKER) continue;
    for (std::size_t level = 0; level < config_.quote_levels; ++level) {
      const auto bid_price = quotePrice(simex::common::Side::BUY, level);
      const auto ask_price = quotePrice(simex::common::Side::SELL, level);
      if (bid_price == simex::common::INVALID_PRICE_TICKS ||
          ask_price == simex::common::INVALID_PRICE_TICKS) {
        continue;
      }
      if (!hasMakerQuote(bot, simex::common::Side::BUY, level)) {
        const auto quantity = randomQuantity();
        if (canOpen(bot, simex::common::Side::BUY, quantity)) {
          requests.push_back(makeNewRequest(bot, simex::common::Side::BUY, bid_price, quantity,
                                            now, true));
          if (!first_batch) return;
        }
      }
      if (!hasMakerQuote(bot, simex::common::Side::SELL, level)) {
        const auto quantity = randomQuantity();
        if (canOpen(bot, simex::common::Side::SELL, quantity)) {
          requests.push_back(makeNewRequest(bot, simex::common::Side::SELL, ask_price, quantity,
                                            now, true));
          if (!first_batch) return;
        }
      }
    }
  }
}

auto ParticipantSimulator::generateTakerRequest(simex::common::Nanos now,
                                                RequestList &requests) -> void {
  if (bots_.empty()) return;
  std::vector<Bot *> takers;
  takers.reserve(config_.taker_count);
  for (auto &bot : bots_) {
    if (bot.role != BotRole::TAKER) continue;
    bool has_pending = false;
    for (const auto &order : bot.orders) {
      if (order.state != OrderState::CANCEL_PENDING) {
        has_pending = true;
        break;
      }
    }
    if (!has_pending) takers.push_back(&bot);
  }
  if (takers.empty()) return;

  std::uniform_int_distribution<std::size_t> bot_distribution(0, takers.size() - 1);
  auto &bot = *takers[bot_distribution(random_)];
  const auto bid = bestBid();
  const auto ask = bestAsk();
  if (!bid && !ask) return;

  std::array<simex::common::Side, 2> sides{simex::common::Side::BUY,
                                           simex::common::Side::SELL};
  std::uniform_int_distribution<int> side_distribution(0, 1);
  const auto first_side = sides[side_distribution(random_)];
  const auto trySide = [&](simex::common::Side side) -> bool {
    const auto price = side == simex::common::Side::BUY ? ask : bid;
    if (!price) return false;
    const auto quantity = randomQuantity();
    if (!canOpen(bot, side, quantity)) return false;
    const auto crossing_price = clampPrice(*price);
    if (crossing_price == simex::common::INVALID_PRICE_TICKS) return false;
    requests.push_back(makeNewRequest(bot, side, crossing_price, quantity, now, false));
    return true;
  };
  if (!trySide(first_side)) (void)trySide(first_side == simex::common::Side::BUY
                                               ? simex::common::Side::SELL
                                               : simex::common::Side::BUY);
}

auto ParticipantSimulator::makeNewRequest(Bot &bot, simex::common::Side side,
                                          simex::common::PriceTicks price,
                                          simex::common::Qty qty,
                                          simex::common::Nanos now,
                                          bool maker_quote) -> simex::exchange::ClientRequest {
  const auto order_id = bot.next_order_id++;
  simex::exchange::ClientRequest request{simex::common::RequestType::NEW,
                                         bot.client_id,
                                         config_.ticker_id,
                                         order_id,
                                         side,
                                         simex::common::OrderType::LIMIT,
                                         simex::common::TimeInForce::DAY,
                                         simex::common::PositionEffect::OPEN,
                                         price,
                                         qty,
                                         now};
  bot.orders.push_back({request, qty, now, OrderState::PENDING, maker_quote});
  ++stats_.generated_requests;
  ++stats_.generated_new_requests;
  return request;
}

auto ParticipantSimulator::makeCancelRequest(Bot &bot, ActiveOrder &order,
                                              simex::common::Nanos now)
    -> simex::exchange::ClientRequest {
  order.state = OrderState::CANCEL_PENDING;
  // TODO(participant): once ClientRequest has target_client_order_id and
  // MODIFY, replace this CANCEL+NEW re-quote operation with a true amend.
  return {simex::common::RequestType::CANCEL,
          bot.client_id,
          config_.ticker_id,
          order.request.client_order_id,
          order.request.side,
          simex::common::OrderType::LIMIT,
          simex::common::TimeInForce::DAY,
          simex::common::PositionEffect::OPEN,
          simex::common::INVALID_PRICE_TICKS,
          0,
          now};
}

auto ParticipantSimulator::onResponse(const simex::exchange::ClientResponse &response) -> void {
  auto *bot = findBot(response.client_id);
  if (bot == nullptr) return;
  auto *order = findOrder(*bot, response.client_order_id);

  switch (response.type) {
    case simex::common::ResponseType::ACCEPTED:
      ++stats_.accepted_orders;
      if (order != nullptr) {
        order->state = OrderState::LIVE;
        order->leaves_qty = response.leaves_qty == 0 ? order->request.qty : response.leaves_qty;
      }
      break;
    case simex::common::ResponseType::REJECTED:
      ++stats_.rejected_orders;
      if (order != nullptr) removeOrder(*bot, response.client_order_id);
      break;
    case simex::common::ResponseType::CANCEL_REJECTED:
      ++stats_.cancel_rejected_orders;
      if (order != nullptr) order->state = OrderState::LIVE;
      break;
    case simex::common::ResponseType::CANCELED:
      ++stats_.canceled_orders;
      if (order != nullptr) removeOrder(*bot, response.client_order_id);
      break;
    case simex::common::ResponseType::FILLED:
      ++stats_.fills;
      stats_.filled_qty += response.exec_qty;
      if (order != nullptr) {
        addPosition(*bot, order->request.side, response.exec_qty);
        order->leaves_qty = response.leaves_qty;
        order->state = OrderState::LIVE;
        if (order->leaves_qty == 0) removeOrder(*bot, response.client_order_id);
      }
      break;
  }
}

auto ParticipantSimulator::onMarketUpdate(const simex::exchange::MarketUpdate &update) -> void {
  if (update.ticker_id != config_.ticker_id ||
      update.market_order_id == simex::common::INVALID_ORDER_ID) {
    return;
  }
  switch (update.type) {
    case simex::common::MarketUpdateType::ADD:
      if (update.leaves_qty > 0) {
        public_orders_[update.market_order_id] = {update.side, update.price_ticks,
                                                   update.leaves_qty};
      }
      break;
    case simex::common::MarketUpdateType::MODIFY: {
      const auto found = public_orders_.find(update.market_order_id);
      if (found != public_orders_.end()) {
        found->second.side = update.side;
        found->second.price_ticks = update.price_ticks;
        found->second.leaves_qty = update.leaves_qty;
        if (update.leaves_qty == 0) public_orders_.erase(found);
      }
      break;
    }
    case simex::common::MarketUpdateType::CANCEL:
      public_orders_.erase(update.market_order_id);
      break;
    case simex::common::MarketUpdateType::TRADE:
      // The public trade record intentionally does not identify the passive
      // order.  The following MODIFY/CANCEL update carries that state.
      break;
  }
}

auto ParticipantSimulator::stats() const noexcept -> SimulatorStats {
  auto result = stats_;
  result.pending_orders = 0;
  result.live_orders = 0;
  for (const auto &bot : bots_) {
    for (const auto &order : bot.orders) {
      if (order.state == OrderState::PENDING) {
        ++result.pending_orders;
      } else if (order.state == OrderState::LIVE) {
        ++result.live_orders;
      }
    }
  }
  result.long_position = aggregatePosition(simex::common::Side::BUY);
  result.short_position = aggregatePosition(simex::common::Side::SELL);
  return result;
}

auto ParticipantSimulator::bestBid() const noexcept
    -> std::optional<simex::common::PriceTicks> {
  std::optional<simex::common::PriceTicks> result;
  for (const auto &[market_order_id, order] : public_orders_) {
    (void)market_order_id;
    if (order.side != simex::common::Side::BUY || order.leaves_qty == 0) continue;
    if (!result || order.price_ticks > *result) result = order.price_ticks;
  }
  return result;
}

auto ParticipantSimulator::bestAsk() const noexcept
    -> std::optional<simex::common::PriceTicks> {
  std::optional<simex::common::PriceTicks> result;
  for (const auto &[market_order_id, order] : public_orders_) {
    (void)market_order_id;
    if (order.side != simex::common::Side::SELL || order.leaves_qty == 0) continue;
    if (!result || order.price_ticks < *result) result = order.price_ticks;
  }
  return result;
}

auto ParticipantSimulator::findBot(simex::common::ClientId client_id) -> Bot * {
  const auto found = std::find_if(bots_.begin(), bots_.end(), [client_id](const auto &bot) {
    return bot.client_id == client_id;
  });
  return found == bots_.end() ? nullptr : &*found;
}

auto ParticipantSimulator::findBot(simex::common::ClientId client_id) const -> const Bot * {
  const auto found = std::find_if(bots_.begin(), bots_.end(), [client_id](const auto &bot) {
    return bot.client_id == client_id;
  });
  return found == bots_.end() ? nullptr : &*found;
}

auto ParticipantSimulator::findOrder(Bot &bot, simex::common::ClientOrderId order_id)
    -> ActiveOrder * {
  const auto found = std::find_if(bot.orders.begin(), bot.orders.end(), [order_id](const auto &order) {
    return order.request.client_order_id == order_id;
  });
  return found == bot.orders.end() ? nullptr : &*found;
}

auto ParticipantSimulator::findOrder(const Bot &bot,
                                     simex::common::ClientOrderId order_id) const
    -> const ActiveOrder * {
  const auto found = std::find_if(bot.orders.begin(), bot.orders.end(), [order_id](const auto &order) {
    return order.request.client_order_id == order_id;
  });
  return found == bot.orders.end() ? nullptr : &*found;
}

auto ParticipantSimulator::removeOrder(Bot &bot,
                                        simex::common::ClientOrderId order_id) -> void {
  bot.orders.erase(std::remove_if(bot.orders.begin(), bot.orders.end(), [order_id](const auto &order) {
                    return order.request.client_order_id == order_id;
                  }),
                  bot.orders.end());
}

auto ParticipantSimulator::pendingQuantity(const Bot &bot, simex::common::Side side) const noexcept
    -> simex::common::Qty {
  std::uint64_t quantity = 0;
  for (const auto &order : bot.orders) {
    if (order.request.side == side) {
      quantity += order.leaves_qty;
    }
  }
  return quantity > std::numeric_limits<simex::common::Qty>::max()
             ? std::numeric_limits<simex::common::Qty>::max()
             : static_cast<simex::common::Qty>(quantity);
}

auto ParticipantSimulator::hasMakerQuote(const Bot &bot, simex::common::Side side,
                                          std::size_t level) const noexcept -> bool {
  const auto price = quotePrice(side, level);
  return std::any_of(bot.orders.begin(), bot.orders.end(), [side, price](const auto &order) {
    return order.maker_quote && order.request.side == side && order.request.price_ticks == price;
  });
}

auto ParticipantSimulator::quotePrice(simex::common::Side side, std::size_t level) const noexcept
    -> simex::common::PriceTicks {
  if (fair_value_ticks_ == simex::common::INVALID_PRICE_TICKS) {
    return simex::common::INVALID_PRICE_TICKS;
  }
  const auto tick = instrument_.tick_size;
  const auto half_spread = std::max<simex::common::PriceTicks>(
      tick, config_.quote_spread_ticks / 2);
  const auto distance = half_spread + safeMultiply(std::max<simex::common::PriceTicks>(tick,
                                                                                         config_.quote_spread_ticks),
                                                    level);
  const auto raw = side == simex::common::Side::BUY ? fair_value_ticks_ - distance
                                                     : fair_value_ticks_ + distance;
  return clampPrice(raw);
}

auto ParticipantSimulator::alignPrice(simex::common::PriceTicks price) const noexcept
    -> simex::common::PriceTicks {
  if (instrument_.tick_size <= 0 || price <= 0) return simex::common::INVALID_PRICE_TICKS;
  return price - price % instrument_.tick_size;
}

auto ParticipantSimulator::clampPrice(simex::common::PriceTicks price) const noexcept
    -> simex::common::PriceTicks {
  if (instrument_.tick_size <= 0 || lower_price_limit_ == simex::common::INVALID_PRICE_TICKS ||
      upper_price_limit_ == simex::common::INVALID_PRICE_TICKS) {
    return simex::common::INVALID_PRICE_TICKS;
  }
  const auto tick = instrument_.tick_size;
  const auto first = ((lower_price_limit_ + tick - 1) / tick) * tick;
  const auto last = (upper_price_limit_ / tick) * tick;
  if (first <= 0 || first > last) return simex::common::INVALID_PRICE_TICKS;
  price = std::clamp(price, first, last);
  price = alignPrice(price);
  if (price < first) price = first;
  return price > last ? last : price;
}

auto ParticipantSimulator::randomQuantity() -> simex::common::Qty {
  std::uniform_int_distribution<std::uint64_t> distribution(config_.min_qty, config_.max_qty);
  return static_cast<simex::common::Qty>(distribution(random_));
}

auto ParticipantSimulator::canOpen(const Bot &bot, simex::common::Side side,
                                   simex::common::Qty qty) const noexcept -> bool {
  if (qty == 0) return false;
  const auto current = side == simex::common::Side::BUY ? bot.long_position : bot.short_position;
  const auto pending = pendingQuantity(bot, side);
  if (current >= config_.max_position || pending >= config_.max_position ||
      current > config_.max_position - pending) {
    return false;
  }
  const auto remaining = static_cast<std::uint64_t>(config_.max_position - current - pending);
  return static_cast<std::uint64_t>(qty) <= remaining;
}

auto ParticipantSimulator::addPosition(Bot &bot, simex::common::Side side,
                                        simex::common::Qty quantity) noexcept -> void {
  if (side == simex::common::Side::BUY) {
    if (std::numeric_limits<simex::common::Qty>::max() - bot.long_position < quantity) {
      bot.long_position = std::numeric_limits<simex::common::Qty>::max();
    } else {
      bot.long_position += quantity;
    }
  } else {
    if (std::numeric_limits<simex::common::Qty>::max() - bot.short_position < quantity) {
      bot.short_position = std::numeric_limits<simex::common::Qty>::max();
    } else {
      bot.short_position += quantity;
    }
  }
}

auto ParticipantSimulator::aggregatePosition(simex::common::Side side) const noexcept
    -> simex::common::Qty {
  std::uint64_t total = 0;
  for (const auto &bot : bots_) {
    const auto quantity = side == simex::common::Side::BUY ? bot.long_position : bot.short_position;
    total += static_cast<std::uint64_t>(quantity);
  }
  return total > std::numeric_limits<simex::common::Qty>::max()
             ? std::numeric_limits<simex::common::Qty>::max()
             : static_cast<simex::common::Qty>(total);
}

}  // namespace simex::participant
