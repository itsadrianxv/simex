#include <cassert>
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
                    int minute) -> Common::Nanos {
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
  const auto config = Common::loadSimexConfig(config_path);

  Exchange::SessionCalendar calendar(config.session_template);
  assert(calendar.addTradingDay({20260925, 3500, true, 20260924}));
  const auto now = localTimestamp(2026, 9, 25, 9 * 60 + 30);

  Exchange::FrontClearing clearing;
  Exchange::ClientResponseQueue responses(1024);
  Exchange::MarketUpdateQueue updates(1024);
  Exchange::MatchingEngine engine(config.instrument, &clearing, &responses, &updates);
  assert(engine.setReferencePrice(3500));
  assert(engine.setPhase(calendar.phaseAt(now)));

  const Exchange::ClientRequest sell{Common::RequestType::NEW, 2, 0, 20,
                                     Common::Side::SELL, Common::OrderType::LIMIT,
                                     Common::TimeInForce::DAY, Common::PositionEffect::OPEN,
                                     3500, 2, now};
  const Exchange::ClientRequest buy{Common::RequestType::NEW, 1, 0, 10,
                                    Common::Side::BUY, Common::OrderType::LIMIT,
                                    Common::TimeInForce::DAY, Common::PositionEffect::OPEN,
                                    3500, 1, now + 1};
  assert(engine.processClientRequest(sell));
  assert(engine.processClientRequest(buy));

  Exchange::MarketDataPublisher publisher(&updates);
  const auto incremental = publisher.publishPending();
  std::cout << "phase=" << Common::sessionPhaseToString(calendar.phaseAt(now))
            << " incremental_updates=" << incremental.size()
            << " state_hash=" << Trace::sha256(engine.canonicalState()) << '\n';
  return 0;
}
