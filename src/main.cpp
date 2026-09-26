#include <chrono>
#include <iostream>

#include "common/config.h"
#include "front_clearing/front_clearing.h"
#include "market_data/market_data_publisher.h"
#include "matching_engine/matching_engine.h"
#include "session_calendar/session_calendar.h"
#include "trace/trace.h"

namespace {

auto localTimestamp(unsigned year_value, unsigned month_value, unsigned day_value,
                    int minute) -> simex::common::Nanos {
  using namespace std::chrono;
  const auto date = year_month_day{year{static_cast<int>(year_value)},
                                   month{month_value}, day{day_value}};
  const auto utc_midnight = sys_days{date}.time_since_epoch();
  return duration_cast<nanoseconds>(utc_midnight + hours{minute / 60} +
                                    minutes{minute % 60} - hours{8})
      .count();
}

}  // namespace

int main(int argc, char **argv) {
  const auto config_path = argc > 1 ? argv[1] : "simex.json";
  const auto config = simex::common::loadSimexConfig(config_path);

  simex::exchange::SessionCalendar calendar(config.session_template);
  if (!calendar.addTradingDay({20260925, 3500, true, 20260924})) {
    return 1;
  }
  const auto now = localTimestamp(2026, 9, 25, 9 * 60 + 30);

  simex::exchange::FrontClearing clearing;
  simex::exchange::ClientResponseQueue responses(1024);
  simex::exchange::MarketUpdateQueue updates(1024);
  simex::exchange::MatchingEngine engine(config.instrument, &clearing, &responses, &updates);
  if (!engine.setReferencePrice(3500) ||
      !engine.setPhase(calendar.phaseAt(now))) {
    return 1;
  }

  const simex::exchange::ClientRequest sell{simex::common::RequestType::NEW, 2, 0, 20,
                                     simex::common::Side::SELL, simex::common::OrderType::LIMIT,
                                     simex::common::TimeInForce::DAY, simex::common::PositionEffect::OPEN,
                                     3500, 2, now};
  const simex::exchange::ClientRequest buy{simex::common::RequestType::NEW, 1, 0, 10,
                                    simex::common::Side::BUY, simex::common::OrderType::LIMIT,
                                    simex::common::TimeInForce::DAY, simex::common::PositionEffect::OPEN,
                                    3500, 1, now + 1};
  if (!engine.processClientRequest(sell) ||
      !engine.processClientRequest(buy)) {
    return 1;
  }

  simex::exchange::MarketDataPublisher publisher(&updates);
  const auto incremental = publisher.publishPending();
  std::cout << "phase=" << simex::common::sessionPhaseToString(calendar.phaseAt(now))
            << " incremental_updates=" << incremental.size()
            << " state_hash=" << simex::trace::sha256(engine.canonicalState()) << '\n';
  return 0;
}
