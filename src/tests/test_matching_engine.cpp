#include <cassert>
#include <vector>

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
  config.supported_order_combinations = {
      {simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY},
      {simex::common::OrderType::LIMIT, simex::common::TimeInForce::IOC},
      {simex::common::OrderType::LIMIT, simex::common::TimeInForce::FOK},
      {simex::common::OrderType::MARKET, simex::common::TimeInForce::DAY},
  };
  return config;
}

auto newOrder(simex::common::ClientId client_id, simex::common::ClientOrderId order_id,
              simex::common::Side side, simex::common::PriceTicks price, simex::common::Qty qty,
              simex::common::Nanos rx_time, simex::common::TimeInForce tif = simex::common::TimeInForce::DAY) {
  return simex::exchange::ClientRequest{simex::common::RequestType::NEW, client_id, 0, order_id, side,
                                simex::common::OrderType::LIMIT, tif, simex::common::PositionEffect::OPEN,
                                price, qty, rx_time};
}

auto drainResponses(simex::exchange::ClientResponseQueue &queue) {
  std::vector<simex::exchange::ClientResponse> result;
  simex::exchange::ClientResponse response;
  while (queue.tryPop(&response)) result.push_back(response);
  return result;
}

auto drainUpdates(simex::exchange::MarketUpdateQueue &queue) {
  std::vector<simex::exchange::MarketUpdate> result;
  simex::exchange::MarketUpdate update;
  while (queue.tryPop(&update)) result.push_back(update);
  return result;
}

}  // namespace

int main() {
  simex::exchange::FrontClearing clearing;
  simex::exchange::ClientResponseQueue responses(64);
  simex::exchange::MarketUpdateQueue updates(64);
  simex::exchange::MatchingEngine engine(instrument(), &clearing, &responses, &updates);
  assert(engine.setReferencePrice(1000));
  engine.setPhase(simex::common::SessionPhase::CONTINUOUS);

  assert(engine.processClientRequest(newOrder(2, 20, simex::common::Side::SELL, 1000, 3, 1)));
  auto first_responses = drainResponses(responses);
  assert(first_responses.size() == 1);
  assert(first_responses.front().type == simex::common::ResponseType::ACCEPTED);
  assert(drainUpdates(updates).size() == 1);

  assert(engine.processClientRequest(newOrder(1, 10, simex::common::Side::BUY, 1010, 2, 2)));
  auto second_responses = drainResponses(responses);
  assert(second_responses.size() == 3);
  assert(second_responses[0].type == simex::common::ResponseType::ACCEPTED);
  assert(second_responses[1].type == simex::common::ResponseType::FILLED);
  assert(second_responses[2].type == simex::common::ResponseType::FILLED);
  assert(second_responses[1].price_ticks == 1000);
  auto second_updates = drainUpdates(updates);
  assert(second_updates.size() == 2);
  assert(second_updates[0].type == simex::common::MarketUpdateType::TRADE);
  assert(second_updates[1].type == simex::common::MarketUpdateType::MODIFY);
  assert(second_updates[1].leaves_qty == 1);

  const auto seller_position = clearing.position(2);
  const auto buyer_position = clearing.position(1);
  assert(seller_position.short_today == 2);
  assert(buyer_position.long_today == 2);

  auto fok = newOrder(3, 30, simex::common::Side::BUY, 1000, 2, 3, simex::common::TimeInForce::FOK);
  assert(engine.processClientRequest(fok));
  const auto fok_responses = drainResponses(responses);
  assert(fok_responses.size() == 1);
  assert(fok_responses.front().type == simex::common::ResponseType::REJECTED);
  assert(fok_responses.front().reason == simex::common::ReasonCode::FOK_NOT_FILLED);
  assert(drainUpdates(updates).empty());

  const simex::exchange::ClientRequest cancel_resting{simex::common::RequestType::CANCEL, 2, 0, 20,
                                              simex::common::Side::SELL, simex::common::OrderType::LIMIT,
                                              simex::common::TimeInForce::DAY, simex::common::PositionEffect::OPEN,
                                              simex::common::INVALID_PRICE_TICKS, 0, 4};
  assert(engine.processClientRequest(cancel_resting));
  drainResponses(responses);
  drainUpdates(updates);
  assert(engine.processClientRequest(newOrder(4, 40, simex::common::Side::SELL, 1000, 1, 4)));
  drainResponses(responses);
  drainUpdates(updates);
  auto close_order = newOrder(2, 21, simex::common::Side::BUY, 1000, 1, 5);
  close_order.position_effect = simex::common::PositionEffect::CLOSE_TODAY;
  assert(engine.processClientRequest(close_order));
  const auto close_responses = drainResponses(responses);
  assert(close_responses.size() == 3);
  assert(close_responses[1].type == simex::common::ResponseType::FILLED);
  assert(clearing.position(2).short_today == 1);
  drainUpdates(updates);

  assert(engine.processClientRequest(newOrder(5, 50, simex::common::Side::SELL, 1000, 1, 6)));
  assert(engine.processClientRequest(newOrder(6, 60, simex::common::Side::SELL, 1001, 1, 7)));
  drainResponses(responses);
  drainUpdates(updates);
  assert(engine.processClientRequest(newOrder(7, 70, simex::common::Side::BUY, 1010, 2, 8)));
  const auto multi_fill_responses = drainResponses(responses);
  assert(multi_fill_responses.size() == 5);
  assert(multi_fill_responses[1].leaves_qty == 1);
  assert(multi_fill_responses[2].leaves_qty == 0);
  assert(multi_fill_responses[3].leaves_qty == 0);
  assert(multi_fill_responses[4].leaves_qty == 0);
  drainUpdates(updates);

  assert(engine.processClientRequest(newOrder(1, 11, simex::common::Side::BUY, 990, 1, 9)));
  drainResponses(responses);
  drainUpdates(updates);
  assert(engine.setPhase(simex::common::SessionPhase::CLOSED));
  assert(engine.book().snapshot().empty());
  const auto end_response = drainResponses(responses);
  assert(end_response.size() == 1);
  assert(end_response.front().reason == simex::common::ReasonCode::SESSION_END);
  return 0;
}
