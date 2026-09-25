#include <cassert>

#include "order_server/fifo_sequencer.h"
#include "participant_harness/participant_harness.h"

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
  config.supported_order_combinations = {
      {Common::OrderType::LIMIT, Common::TimeInForce::DAY},
      {Common::OrderType::LIMIT, Common::TimeInForce::IOC},
      {Common::OrderType::LIMIT, Common::TimeInForce::FOK},
      {Common::OrderType::MARKET, Common::TimeInForce::DAY},
  };
  return config;
}

auto order(Common::ClientId client_id, Common::ClientOrderId order_id,
           Common::Side side, Common::PriceTicks price, Common::Nanos rx_time,
           Common::Qty qty = 1) {
  return Exchange::ClientRequest{Common::RequestType::NEW, client_id, 0, order_id, side,
                                 Common::OrderType::LIMIT, Common::TimeInForce::DAY,
                                 Common::PositionEffect::OPEN, price, qty, rx_time};
}

}  // namespace

int main() {
  Common::LFQueue<Exchange::ClientRequest> sequenced(1);
  Exchange::FIFOSequencer sequencer(&sequenced);
  assert(sequencer.addClientRequest(order(1, 1, Common::Side::BUY, 1000, 2)));
  assert(sequencer.addClientRequest(order(1, 2, Common::Side::BUY, 1001, 1)));
  assert(!sequencer.sequenceAndPublish());
  assert(sequenced.empty());

  Harness::ParticipantHarness harness(instrument());
  assert(harness.setReferencePrice(1000));
  harness.setPhase(Common::SessionPhase::CONTINUOUS);

  assert(harness.submit(order(2, 20, Common::Side::SELL, 1000, 1, 2)));
  assert(harness.drainResponses().size() == 1);
  harness.drainUpdates();

  assert(harness.receive(order(1, 10, Common::Side::BUY, 1000, 2)));
  assert(harness.flush());
  const auto responses = harness.drainResponses();
  assert(responses.size() == 3);
  assert(responses[1].type == Common::ResponseType::FILLED);
  assert(responses[2].type == Common::ResponseType::FILLED);
  const auto updates = harness.drainUpdates();
  assert(updates.size() == 2);
  assert(updates[0].type == Common::MarketUpdateType::TRADE);
  assert(updates[1].type == Common::MarketUpdateType::MODIFY);
  const auto first_hash = harness.stateHash();
  assert(first_hash.rfind("sha256:", 0) == 0);
  const auto trace = harness.traceRecord();
  assert(trace.requests.size() == 2);
  assert(!trace.private_responses.empty());
  assert(harness.replayTrace(trace));

  return 0;
}
