#include <cassert>
#include <chrono>
#include <thread>

#include "runtime/exchange_runtime.h"

namespace {

auto instrument() -> simex::common::InstrumentConfig {
  simex::common::InstrumentConfig config;
  config.symbol = "RB";
  config.exchange = "SHFE";
  config.tick_size = 1;
  config.price_limit_percent = 0.1;
  config.max_price_levels = 1000;
  config.supported_order_combinations = {
      {simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY}};
  return config;
}

auto simulatorConfig() -> simex::participant::ParticipantSimulatorConfig {
  simex::participant::ParticipantSimulatorConfig config;
  config.enabled = true;
  config.seed = 7;
  config.client_id_start = 100;
  config.market_maker_count = 1;
  config.taker_count = 1;
  config.orders_per_second = 1;
  config.quote_levels = 1;
  config.quote_spread_ticks = 2;
  config.quote_ttl_nanos = 10;
  config.min_qty = 1;
  config.max_qty = 1;
  config.max_position = 4;
  return config;
}

}  // namespace

int main() {
  simex::runtime::ExchangeRuntimeConfig config;
  config.instrument = instrument();
  config.reference_price = 1000;
  config.trading_day = 20260926;
  config.initial_time = 1;
  config.initial_phase = simex::common::SessionPhase::CONTINUOUS;
  config.clock_mode = simex::runtime::ClockMode::MANUAL;
  config.queue_capacity = 128;
  config.participant_simulator = simulatorConfig();
  config.external_client_id = 1;
  config.sessions = {{11, config.trading_day, simex::common::SessionPhase::CLOSED},
                     {21, config.trading_day, simex::common::SessionPhase::CONTINUOUS}};

  simex::runtime::ExchangeRuntime runtime(config);
  assert(runtime.start());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto stats = runtime.simulatorStats();
  assert(stats.generated_new_requests > 0);
  assert(stats.accepted_orders > 0);
  simex::exchange::ClientResponse response;
  while (runtime.responses().tryPop(&response)) {
    assert(response.client_id == config.external_client_id);
  }

  // A phase transition cancels the internal DAY orders, and a later
  // transition lets the runtime create fresh quotes again.
  assert(runtime.advanceTo(15));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto paused = runtime.simulatorStats();
  assert(paused.canceled_orders > 0);
  assert(runtime.advanceTo(25));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto resumed = runtime.simulatorStats();
  assert(resumed.generated_new_requests >= paused.generated_new_requests + 2);

  bool saw_update = false;
  simex::exchange::MarketUpdate update;
  while (runtime.updates().tryPop(&update)) saw_update = true;
  assert(saw_update);
  runtime.stop();
  assert(runtime.state() == simex::runtime::RuntimeState::STOPPED);
  return 0;
}
