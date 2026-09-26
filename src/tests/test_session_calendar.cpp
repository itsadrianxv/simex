#include <cassert>
#include <chrono>
#include <vector>

#include "session_calendar/session_calendar.h"

namespace {

auto localTimestamp(unsigned year_value, unsigned month_value, unsigned day_value,
                    int minute) -> Common::Nanos {
  using namespace std::chrono;
  const auto date = year_month_day{std::chrono::year{static_cast<int>(year_value)},
                                   std::chrono::month{month_value},
                                   std::chrono::day{day_value}};
  const auto utc_midnight = sys_days{date}.time_since_epoch();
  return duration_cast<nanoseconds>(utc_midnight + hours{minute / 60} + minutes{minute % 60} - hours{8}).count();
}

auto rbTemplate() -> Common::SessionTemplateConfig {
  Common::SessionTemplateConfig config;
  config.night = {{Common::SessionPhase::AUCTION_SUBMIT, 20 * 60 + 55, 21 * 60 - 1, false},
                  {Common::SessionPhase::AUCTION_MATCH, 21 * 60 - 1, 21 * 60, false},
                  {Common::SessionPhase::CONTINUOUS, 21 * 60, 23 * 60, false}};
  config.overnight_closed = {Common::SessionPhase::CLOSED, 23 * 60, 8 * 60 + 55, true};
  config.day = {{Common::SessionPhase::AUCTION_SUBMIT, 8 * 60 + 55, 8 * 60 + 59, false},
                {Common::SessionPhase::AUCTION_MATCH, 8 * 60 + 59, 9 * 60, false},
                {Common::SessionPhase::CONTINUOUS, 9 * 60, 10 * 60 + 15, false},
                {Common::SessionPhase::BREAK, 10 * 60 + 15, 10 * 60 + 30, false},
                {Common::SessionPhase::CONTINUOUS, 10 * 60 + 30, 11 * 60 + 30, false},
                {Common::SessionPhase::BREAK, 11 * 60 + 30, 13 * 60 + 30, false},
                {Common::SessionPhase::CONTINUOUS, 13 * 60 + 30, 15 * 60, false},
                {Common::SessionPhase::CLOSED, 15 * 60, 20 * 60 + 55, false}};
  return config;
}

}  // namespace

int main() {
  Exchange::SessionCalendar calendar(rbTemplate());
  assert(calendar.addTradingDay({20260925, 3500, true, 20260924}));
  assert(calendar.phaseAt(localTimestamp(2026, 9, 24, 20 * 60 + 56)) ==
         Common::SessionPhase::AUCTION_SUBMIT);
  assert(calendar.phaseAt(localTimestamp(2026, 9, 25, 9 * 60 + 30)) ==
         Common::SessionPhase::CONTINUOUS);
  assert(calendar.phaseAt(localTimestamp(2026, 9, 25, 12 * 60)) ==
         Common::SessionPhase::BREAK);
  assert(calendar.phaseAt(localTimestamp(2026, 9, 25, 16 * 60)) ==
         Common::SessionPhase::CLOSED);
  const auto next = calendar.nextTransitionAfter(localTimestamp(2026, 9, 25, 9 * 60));
  assert(next.has_value());
  assert(next->phase == Common::SessionPhase::BREAK);

  Common::VirtualClock clock(localTimestamp(2026, 9, 24, 20 * 60 + 54), 20260924);
  assert(calendar.scheduleInto(clock, 20260925));
  std::vector<Common::VirtualClock::Event> events;
  assert(clock.advanceTo(localTimestamp(2026, 9, 25, 9 * 60),
                         [&](const auto &event) { events.push_back(event); }));
  assert(!events.empty());
  assert(events.front().type == Common::VirtualClock::EventType::SESSION_PHASE);
  assert(events.back().phase == Common::SessionPhase::CONTINUOUS);
  return 0;
}
