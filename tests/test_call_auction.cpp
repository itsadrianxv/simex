#include <cassert>

#include "matching_engine/matching_engine.h"

namespace {

auto instrument() -> Common::InstrumentConfig {
  Common::InstrumentConfig config;
  config.symbol = "RB";
  config.profile = "test";
  config.exchange = "SHFE";
  config.contract_size_tons = 10;
  config.tick_size = 1;
  config.price_limit_percent = 0.10;
  config.max_price_levels = 1000;
  config.supported_order_combinations = {{Common::OrderType::LIMIT, Common::TimeInForce::DAY}};
  return config;
}

auto order(Common::ClientId client_id, Common::ClientOrderId order_id,
           Common::Side side, Common::PriceTicks price, Common::Qty qty,
           Common::Nanos rx_time) {
  return Exchange::ClientRequest{Common::RequestType::NEW, client_id, 0, order_id, side,
                                 Common::OrderType::LIMIT, Common::TimeInForce::DAY,
                                 Common::PositionEffect::OPEN, price, qty, rx_time};
}

}  // namespace

int main() {
  Exchange::FrontClearing clearing;
  Exchange::ClientResponseQueue responses(64);
  Exchange::MarketUpdateQueue updates(64);
  Exchange::MatchingEngine engine(instrument(), &clearing, &responses, &updates);
  assert(engine.setReferencePrice(1000));
  assert(engine.setPhase(Common::SessionPhase::AUCTION_SUBMIT));
  assert(engine.processClientRequest(order(1, 10, Common::Side::BUY, 1005, 1, 1)));
  assert(engine.processClientRequest(order(2, 20, Common::Side::SELL, 1000, 2, 2)));

  Exchange::ClientResponse response;
  while (responses.tryPop(&response)) {
    assert(response.type == Common::ResponseType::ACCEPTED);
  }
  Exchange::MarketUpdate update;
  while (updates.tryPop(&update)) {
  }

  assert(engine.setPhase(Common::SessionPhase::AUCTION_MATCH));
  std::size_t fills = 0;
  while (responses.tryPop(&response)) {
    if (response.type == Common::ResponseType::FILLED) {
      ++fills;
      assert(response.price_ticks == 1000);
    }
  }
  assert(fills == 2);
  assert(engine.setPhase(Common::SessionPhase::CONTINUOUS));
  const auto remaining = engine.book().snapshot();
  assert(remaining.size() == 1);
  assert(remaining.front().request.side == Common::Side::SELL);
  assert(remaining.front().leaves_qty == 1);
  assert(clearing.position(1).long_today == 1);
  assert(clearing.position(2).short_today == 1);
  return 0;
}
