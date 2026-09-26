#include <cassert>

#include "order_server/fifo_sequencer.h"
#include "participant_harness/participant_harness.h"

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

auto order(simex::common::ClientId client_id, simex::common::ClientOrderId order_id,
           simex::common::Side side, simex::common::PriceTicks price, simex::common::Nanos rx_time,
           simex::common::Qty qty = 1) {
  return simex::exchange::ClientRequest{simex::common::RequestType::NEW, client_id, 0, order_id, side,
                                 simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY,
                                 simex::common::PositionEffect::OPEN, price, qty, rx_time};
}

}  // namespace

int main() {
  simex::common::LFQueue<simex::exchange::ClientRequest> sequenced(1);
  simex::exchange::FIFOSequencer sequencer(&sequenced);
  assert(sequencer.addClientRequest(order(1, 1, simex::common::Side::BUY, 1000, 2)));
  assert(sequencer.addClientRequest(order(1, 2, simex::common::Side::BUY, 1001, 1)));
  assert(!sequencer.sequenceAndPublish());
  assert(sequenced.empty());

  simex::harness::ParticipantHarness harness(instrument());
  assert(harness.setReferencePrice(1000));
  harness.setPhase(simex::common::SessionPhase::CONTINUOUS);

  assert(harness.submit(order(2, 20, simex::common::Side::SELL, 1000, 1, 2)));
  assert(harness.drainResponses().size() == 1);
  harness.drainUpdates();

  assert(harness.receive(order(1, 10, simex::common::Side::BUY, 1000, 2)));
  assert(harness.flush());
  const auto responses = harness.drainResponses();
  assert(responses.size() == 3);
  assert(responses[1].type == simex::common::ResponseType::FILLED);
  assert(responses[2].type == simex::common::ResponseType::FILLED);
  const auto updates = harness.drainUpdates();
  assert(updates.size() == 2);
  assert(updates[0].type == simex::common::MarketUpdateType::TRADE);
  assert(updates[1].type == simex::common::MarketUpdateType::MODIFY);
  const auto first_hash = harness.stateHash();
  assert(first_hash.rfind("sha256:", 0) == 0);
  const auto trace = harness.traceRecord();
  assert(trace.requests.size() == 2);
  assert(!trace.private_responses.empty());
  assert(harness.replayTrace(trace));

  return 0;
}
