#include <cassert>
#include <filesystem>

#include "trace/trace.h"

int main() {
  simex::trace::TraceRecord record;
  record.scenario = "trace_round_trip";
  record.config = "simex.json";
  record.initial_trading_day = 20260925;
  record.previous_settlement_ticks = 3500;
  record.requests.push_back({simex::common::RequestType::NEW, 1, 0, 10, simex::common::Side::BUY,
                             simex::common::OrderType::LIMIT, simex::common::TimeInForce::DAY,
                             simex::common::PositionEffect::OPEN, 3499, 2, 100});
  record.private_responses.push_back({simex::common::ResponseType::ACCEPTED,
                                      simex::common::ReasonCode::NONE,
                                      1, 0, 10, 100, simex::common::Side::BUY,
                                      simex::common::PositionEffect::OPEN, 3499, 0, 2});
  record.public_updates.push_back({simex::common::MarketUpdateType::ADD, 0, 100,
                                   simex::common::Side::BUY, 3499, 2, 2, 100});
  const auto path = std::filesystem::temp_directory_path() / "simex-trace-test.json";
  simex::trace::write(path, record);
  const auto loaded = simex::trace::read(path);
  assert(loaded.scenario == record.scenario);
  assert(loaded.initial_trading_day == record.initial_trading_day);
  assert(loaded.requests.size() == 1);
  assert(loaded.requests.front().client_order_id == 10);
  assert(loaded.private_responses.size() == 1);
  assert(loaded.private_responses.front().type == simex::common::ResponseType::ACCEPTED);
  assert(loaded.public_updates.size() == 1);
  assert(loaded.public_updates.front().type == simex::common::MarketUpdateType::ADD);
  assert(simex::trace::sha256("abc") ==
         "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::filesystem::remove(path);
  return 0;
}
