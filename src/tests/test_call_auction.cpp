#include <cassert>

#include "matching_engine/matching_engine.h"

namespace {

auto instrument() -> simex::common::InstrumentConfig {
  simex::common::InstrumentConfig config;
  config.symbol = "RB";
  config.profile = "test";
  config.exchange = "SHFE";
  config.contract_size_tons = 10;
  config.tick_size = 1;
  config.price_limit_percent = 0.10;
  config.max_price_levels = 1000;
  config.supported_order_combinations = {{simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY}};
  return config;
}

auto order(simex::common::ClientId client_id, simex::common::ClientOrderId order_id,
           simex::common::Side side, simex::common::PriceTicks price, simex::common::Qty qty,
           simex::common::Nanos rx_time) {
  return simex::exchange::ClientRequest{simex::common::RequestType::NEW, client_id, 0, order_id, side,
                                 simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY,
                                 simex::common::PositionEffect::OPEN, price, qty, rx_time};
}

}  // namespace

int main() {
  simex::exchange::FrontClearing clearing;
  simex::exchange::ClientResponseQueue responses(64);
  simex::exchange::MarketUpdateQueue updates(64);
  simex::exchange::MatchingEngine engine(instrument(), &clearing, &responses, &updates);
  assert(engine.setReferencePrice(1000));
  assert(engine.setPhase(simex::common::SessionPhase::AUCTION_SUBMIT));
  assert(engine.processClientRequest(order(1, 10, simex::common::Side::BUY, 1005, 1, 1)));
  assert(engine.processClientRequest(order(2, 20, simex::common::Side::SELL, 1000, 2, 2)));

  simex::exchange::ClientResponse response;
  while (responses.tryPop(&response)) {
    assert(response.type == simex::common::ResponseType::ACCEPTED);
  }
  simex::exchange::MarketUpdate update;
  while (updates.tryPop(&update)) {
  }

  assert(engine.setPhase(simex::common::SessionPhase::AUCTION_MATCH));
  std::size_t fills = 0;
  while (responses.tryPop(&response)) {
    if (response.type == simex::common::ResponseType::FILLED) {
      ++fills;
      assert(response.price_ticks == 1000);
    }
  }
  assert(fills == 2);
  assert(engine.setPhase(simex::common::SessionPhase::CONTINUOUS));
  const auto remaining = engine.book().snapshot();
  assert(remaining.size() == 1);
  assert(remaining.front().request.side == simex::common::Side::SELL);
  assert(remaining.front().leaves_qty == 1);
  assert(clearing.position(1).long_today == 1);
  assert(clearing.position(2).short_today == 1);
  return 0;
}
