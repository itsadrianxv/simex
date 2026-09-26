#include <cassert>
#include <chrono>
#include <thread>
#include "runtime/exchange_runtime.h"

static auto instrument() -> simex::common::InstrumentConfig {
  simex::common::InstrumentConfig c; c.symbol = "RB"; c.exchange = "SHFE"; c.tick_size = 1; c.price_limit_percent = 0.1; c.max_price_levels = 1000; c.supported_order_combinations = {{simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY}}; return c;
}
int main() {
  simex::runtime::ExchangeRuntimeConfig config; config.instrument = instrument(); config.reference_price = 1000; config.trading_day = 1; config.initial_phase = simex::common::SessionPhase::CONTINUOUS; config.queue_capacity = 8; config.clock_mode = simex::runtime::ClockMode::MANUAL;
  simex::runtime::ExchangeRuntime runtime(config); assert(runtime.start());
  simex::exchange::ClientRequest request{simex::common::RequestType::NEW, 1, 0, 7, simex::common::Side::BUY, simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY, simex::common::PositionEffect::OPEN, 1000, 1, 1};
  assert(runtime.submit(request));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  simex::exchange::ClientResponse response; bool received = false; while (runtime.responses().tryPop(&response)) received = true;
  assert(received); assert(response.type == simex::common::ResponseType::ACCEPTED);
  assert(runtime.advanceBy(10));
  runtime.stop(); assert(runtime.state() == simex::runtime::RuntimeState::STOPPED);
  return 0;
}
