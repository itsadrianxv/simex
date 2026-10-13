#include <cassert>
#include <vector>

#include "common/virtual_clock.h"

int main() {
  simex::common::VirtualClock clock(100, 20260925);
  assert(clock.scheduleTradingDayRollover(200, 20260928));

  std::vector<simex::common::VirtualClock::Event> events;
  assert(clock.advanceTo(199, [&](const auto &event) { events.push_back(event); }));
  assert(clock.now() == 199);
  assert(clock.tradingDay() == 20260925);
  assert(events.empty());

  assert(clock.advanceBy(1, [&](const auto &event) { events.push_back(event); }));
  assert(clock.now() == 200);
  assert(clock.tradingDay() == 20260928);
  assert(events.size() == 1);
  assert(events.front().type == simex::common::VirtualClock::EventType::TRADING_DAY_ROLLOVER);

  assert(!clock.advanceTo(199, [&](const auto &) {}));
  assert(clock.setTime(201));
  assert(clock.now() == 201);
  return 0;
}
