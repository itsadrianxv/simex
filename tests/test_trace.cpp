#include <cassert>
#include <filesystem>

#include "trace/trace.h"

int main() {
  Trace::TraceRecord record;
  record.scenario = "trace_round_trip";
  record.config = "simex.json";
  record.initial_trading_day = 20260925;
  record.previous_settlement_ticks = 3500;
  record.requests.push_back({Common::RequestType::NEW, 1, 0, 10, Common::Side::BUY,
                             Common::OrderType::LIMIT, Common::TimeInForce::DAY,
                             Common::PositionEffect::OPEN, 3499, 2, 100});
  record.private_responses.push_back({Common::ResponseType::ACCEPTED,
                                      Common::ReasonCode::NONE,
                                      1, 0, 10, 100, Common::Side::BUY,
                                      Common::PositionEffect::OPEN, 3499, 0, 2});
  record.public_updates.push_back({Common::MarketUpdateType::ADD, 0, 100,
                                   Common::Side::BUY, 3499, 2, 2, 100});
  const auto path = std::filesystem::temp_directory_path() / "simex-trace-test.json";
  Trace::write(path, record);
  const auto loaded = Trace::read(path);
  assert(loaded.scenario == record.scenario);
  assert(loaded.initial_trading_day == record.initial_trading_day);
  assert(loaded.requests.size() == 1);
  assert(loaded.requests.front().client_order_id == 10);
  assert(loaded.private_responses.size() == 1);
  assert(loaded.private_responses.front().type == Common::ResponseType::ACCEPTED);
  assert(loaded.public_updates.size() == 1);
  assert(loaded.public_updates.front().type == Common::MarketUpdateType::ADD);
  assert(Trace::sha256("abc") ==
         "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::filesystem::remove(path);
  return 0;
}
