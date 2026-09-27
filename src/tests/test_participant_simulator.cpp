#include <cassert>
#include <vector>

#include "participant_simulator/participant_simulator.h"

namespace {

auto instrument() -> simex::common::InstrumentConfig {
  simex::common::InstrumentConfig config;
  config.symbol = "RB";
  config.profile = "test";
  config.exchange = "SHFE";
  config.tick_size = 1;
  config.price_limit_percent = 0.10;
  config.max_price_levels = 1000;
  config.supported_order_combinations = {
      {simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY},
  };
  return config;
}

auto config() -> simex::participant::ParticipantSimulatorConfig {
  simex::participant::ParticipantSimulatorConfig result;
  result.enabled = true;
  result.seed = 17;
  result.client_id_start = 100;
  result.market_maker_count = 1;
  result.taker_count = 0;
  result.orders_per_second = 1'000;
  result.quote_levels = 1;
  result.quote_spread_ticks = 2;
  result.quote_ttl_nanos = 100;
  result.min_qty = 1;
  result.max_qty = 1;
  result.max_position = 10;
  return result;
}

auto accepted(const simex::exchange::ClientRequest &request,
              simex::common::MarketOrderId market_order_id = 1)
    -> simex::exchange::ClientResponse {
  return {simex::common::ResponseType::ACCEPTED,
          simex::common::ReasonCode::NONE,
          request.client_id,
          request.ticker_id,
          request.client_order_id,
          market_order_id,
          request.side,
          request.position_effect,
          request.price_ticks,
          0,
          request.qty};
}

auto filled(const simex::exchange::ClientRequest &request,
            simex::common::Qty quantity,
            simex::common::Qty leaves = 0) -> simex::exchange::ClientResponse {
  return {simex::common::ResponseType::FILLED,
          simex::common::ReasonCode::NONE,
          request.client_id,
          request.ticker_id,
          request.client_order_id,
          1,
          request.side,
          request.position_effect,
          request.price_ticks,
          quantity,
          leaves};
}

void deterministicAndSessionGating() {
  auto first_config = config();
  auto second_config = first_config;
  simex::participant::ParticipantSimulator first(instrument(), first_config, 1000);
  simex::participant::ParticipantSimulator second(instrument(), second_config, 1000);

  assert(first.tick(0, simex::common::SessionPhase::CLOSED).empty());
  assert(first.tick(0, simex::common::SessionPhase::AUCTION_SUBMIT).empty());
  const auto first_batch = first.tick(0, simex::common::SessionPhase::CONTINUOUS);
  const auto second_batch = second.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(first_batch.size() == 2);
  assert(first_batch == second_batch);
  assert(first_batch[0].client_id == 100);
  assert(first_batch[1].client_id == 100);
  assert(first_batch[0].client_order_id != first_batch[1].client_order_id);
  assert(first_batch[0].order_type == simex::common::OrderType::LIMIT);
  assert(first_batch[0].time_in_force == simex::common::TimeInForce::DAY);
  assert(first_batch[0].position_effect == simex::common::PositionEffect::OPEN);
  assert(first.tick(1, simex::common::SessionPhase::BREAK).empty());
}

void quoteSpreadIsTotalBboWidth() {
  auto one_tick_config = config();
  one_tick_config.quote_spread_ticks = 1;
  simex::participant::ParticipantSimulator one_tick(instrument(), one_tick_config, 1000);
  const auto one_tick_quotes = one_tick.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(one_tick_quotes.size() == 2);
  assert(one_tick_quotes[0].side == simex::common::Side::BUY);
  assert(one_tick_quotes[1].side == simex::common::Side::SELL);
  assert(one_tick_quotes[1].price_ticks - one_tick_quotes[0].price_ticks == 1);
  assert(one_tick_quotes[0].price_ticks == 1000);
  assert(one_tick_quotes[1].price_ticks == 1001);

  auto two_tick_config = config();
  simex::participant::ParticipantSimulator two_tick(instrument(), two_tick_config, 1000);
  const auto two_tick_quotes = two_tick.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(two_tick_quotes.size() == 2);
  assert(two_tick_quotes[1].price_ticks - two_tick_quotes[0].price_ticks == 2);
  assert(two_tick_quotes[0].price_ticks == 999);
  assert(two_tick_quotes[1].price_ticks == 1001);
}

void responseStateAndTtlReplacement() {
  auto simulator_config = config();
  simulator_config.orders_per_second = 1'000'000'000;
  simex::participant::ParticipantSimulator simulator(instrument(), simulator_config, 1000);
  const auto initial = simulator.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(initial.size() == 2);
  simulator.onResponse(accepted(initial[0], 10));
  simulator.onResponse(accepted(initial[1], 11));
  auto stats = simulator.stats();
  assert(stats.accepted_orders == 2);
  assert(stats.live_orders == 2);

  const auto first_cancel = simulator.tick(100, simex::common::SessionPhase::CONTINUOUS);
  assert(first_cancel.size() == 1);
  assert(first_cancel.front().type == simex::common::RequestType::CANCEL);
  simulator.onResponse({simex::common::ResponseType::CANCELED,
                        simex::common::ReasonCode::NONE,
                        first_cancel.front().client_id,
                        0,
                        first_cancel.front().client_order_id,
                        10,
                        first_cancel.front().side,
                        simex::common::PositionEffect::OPEN,
                        simex::common::INVALID_PRICE_TICKS,
                        0,
                        0});
  const auto second_cancel = simulator.tick(101, simex::common::SessionPhase::CONTINUOUS);
  assert(second_cancel.size() == 1);
  assert(second_cancel.front().type == simex::common::RequestType::CANCEL);
  simulator.onResponse({simex::common::ResponseType::CANCELED,
                        simex::common::ReasonCode::NONE,
                        second_cancel.front().client_id,
                        0,
                        second_cancel.front().client_order_id,
                        11,
                        second_cancel.front().side,
                        simex::common::PositionEffect::OPEN,
                        simex::common::INVALID_PRICE_TICKS,
                        0,
                        0});
  const auto replacement = simulator.tick(102, simex::common::SessionPhase::CONTINUOUS);
  assert(replacement.size() == 1);
  assert(replacement.front().type == simex::common::RequestType::NEW);
  assert(replacement.front().client_order_id > 2);
  stats = simulator.stats();
  assert(stats.canceled_orders == 2);
}

void bboDrivenTakerAndDistinctClientIds() {
  auto simulator_config = config();
  simulator_config.taker_count = 1;
  simulator_config.client_id_start = 200;
  simulator_config.quote_ttl_nanos = 1'000'000'000;
  simex::participant::ParticipantSimulator simulator(instrument(), simulator_config, 1000);
  const auto makers = simulator.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(makers.size() == 2);
  for (std::size_t index = 0; index < makers.size(); ++index) {
    simulator.onResponse(accepted(makers[index], static_cast<simex::common::MarketOrderId>(index + 10)));
    simulator.onMarketUpdate({simex::common::MarketUpdateType::ADD,
                              0,
                              static_cast<simex::common::MarketOrderId>(index + 10),
                              makers[index].side,
                              makers[index].price_ticks,
                              makers[index].qty,
                              makers[index].qty,
                              makers[index].rx_time});
  }
  assert(simulator.bestBid().has_value());
  assert(simulator.bestAsk().has_value());
  const auto taker = simulator.tick(1'000'000, simex::common::SessionPhase::CONTINUOUS);
  assert(taker.size() == 1);
  assert(taker.front().client_id == 201);
  assert(taker.front().order_type == simex::common::OrderType::LIMIT);
  if (taker.front().side == simex::common::Side::BUY) {
    assert(taker.front().price_ticks >= *simulator.bestAsk());
  } else {
    assert(taker.front().price_ticks <= *simulator.bestBid());
  }
}

void grossPositionLimit() {
  auto simulator_config = config();
  simulator_config.min_qty = 2;
  simulator_config.max_qty = 2;
  simulator_config.max_position = 2;
  simex::participant::ParticipantSimulator simulator(instrument(), simulator_config, 1000);
  const auto requests = simulator.tick(0, simex::common::SessionPhase::CONTINUOUS);
  assert(requests.size() == 2);
  for (const auto &request : requests) {
    simulator.onResponse(accepted(request));
    simulator.onResponse(filled(request, 2));
  }
  const auto stats = simulator.stats();
  assert(stats.long_position == 2);
  assert(stats.short_position == 2);
  assert(stats.live_orders == 0);
  assert(simulator.tick(1'000'000'000, simex::common::SessionPhase::CONTINUOUS).empty());
}

}  // namespace

int main() {
  deterministicAndSessionGating();
  quoteSpreadIsTotalBboWidth();
  responseStateAndTtlReplacement();
  bboDrivenTakerAndDistinctClientIds();
  grossPositionLimit();
  return 0;
}
