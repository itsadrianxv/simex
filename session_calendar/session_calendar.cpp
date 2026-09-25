#include "session_calendar/session_calendar.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace Exchange {
namespace {

using namespace std::chrono;

auto parseTradingDay(Common::TradingDayId id) -> year_month_day {
  const auto year_value = static_cast<int>(id / 10000);
  const auto month_value = static_cast<unsigned>(id / 100 % 100);
  const auto day_value = static_cast<unsigned>(id % 100);
  return year_month_day{std::chrono::year{year_value},
                        std::chrono::month{month_value},
                        std::chrono::day{day_value}};
}

auto tradingDayId(const year_month_day &date) -> Common::TradingDayId {
  return static_cast<Common::TradingDayId>(static_cast<int>(date.year()) * 10000U +
                                           static_cast<unsigned>(date.month()) * 100U +
                                           static_cast<unsigned>(date.day()));
}

auto localTimestamp(Common::TradingDayId day, int minute) -> Common::Nanos {
  const auto utc_midnight = sys_days{parseTradingDay(day)}.time_since_epoch();
  const auto local_offset = hours{8};
  const auto utc_time = utc_midnight + hours{minute / 60} + minutes{minute % 60} - local_offset;
  return duration_cast<nanoseconds>(utc_time).count();
}

auto previousCalendarDay(Common::TradingDayId day) -> Common::TradingDayId {
  return tradingDayId(year_month_day{sys_days{parseTradingDay(day)} - days{1}});
}

auto addWindow(std::vector<SessionCalendar::Interval> *intervals,
               Common::TradingDayId trading_day,
               Common::TradingDayId session_date,
               const Common::SessionWindowConfig &window) -> void {
  auto start = localTimestamp(session_date, window.start_minute);
  auto end = localTimestamp(session_date, window.end_minute);
  if (window.crosses_midnight) {
    end += 24LL * 60LL * 60LL * 1000000000LL;
  }
  if (end <= start) {
    return;
  }
  intervals->push_back({start, end, trading_day, window.phase});
}

}  // namespace

SessionCalendar::SessionCalendar(Common::SessionTemplateConfig template_config)
    : template_config_(std::move(template_config)) {}

auto SessionCalendar::addTradingDay(const TradingDaySchedule &schedule) -> bool {
  if (schedule.trading_day == 0 || schedule.previous_settlement_ticks == Common::INVALID_PRICE_TICKS) {
    return false;
  }
  if (scheduleFor(schedule.trading_day) != nullptr) {
    return false;
  }

  const auto night_day = schedule.night_session_date.value_or(previousCalendarDay(schedule.trading_day));
  if (schedule.night_session_enabled) {
    for (const auto &window : template_config_.night) {
      addWindow(&intervals_, schedule.trading_day, night_day, window);
    }
  }

  addWindow(&intervals_, schedule.trading_day, night_day, template_config_.overnight_closed);
  for (const auto &window : template_config_.day) {
    addWindow(&intervals_, schedule.trading_day, schedule.trading_day, window);
  }

  schedules_.push_back(schedule);
  std::sort(intervals_.begin(), intervals_.end(), [](const auto &left, const auto &right) {
    if (left.start != right.start) return left.start < right.start;
    return left.end < right.end;
  });
  return true;
}

auto SessionCalendar::clear() noexcept -> void {
  schedules_.clear();
  intervals_.clear();
}

auto SessionCalendar::scheduleInto(Common::VirtualClock &clock,
                                   Common::TradingDayId trading_day) const -> bool {
  bool scheduled = false;
  for (const auto &interval : intervals_) {
    if (interval.trading_day != trading_day) continue;
    if (!clock.scheduleSessionPhase(interval.start, interval.trading_day, interval.phase)) {
      return false;
    }
    scheduled = true;
  }
  return scheduled;
}

auto SessionCalendar::scheduleRollover(Common::VirtualClock &clock,
                                       Common::Nanos timestamp,
                                       Common::TradingDayId next_trading_day) const -> bool {
  return clock.scheduleTradingDayRollover(timestamp, next_trading_day);
}

auto SessionCalendar::phaseAt(Common::Nanos timestamp) const noexcept -> Common::SessionPhase {
  for (const auto &interval : intervals_) {
    if (interval.start <= timestamp && timestamp < interval.end) {
      return interval.phase;
    }
  }
  return Common::SessionPhase::CLOSED;
}

auto SessionCalendar::nextTransitionAfter(Common::Nanos timestamp) const
    -> std::optional<SessionTransition> {
  std::optional<SessionTransition> next;
  for (const auto &interval : intervals_) {
    if (interval.start > timestamp &&
        (!next || interval.start < next->timestamp ||
         (interval.start == next->timestamp && next->phase == Common::SessionPhase::CLOSED))) {
      next = SessionTransition{interval.start, interval.trading_day, interval.phase};
    }
    if (interval.end > timestamp && (!next || interval.end < next->timestamp)) {
      next = SessionTransition{interval.end, interval.trading_day, Common::SessionPhase::CLOSED};
    }
  }
  return next;
}

auto SessionCalendar::scheduleFor(Common::TradingDayId trading_day) const
    -> const TradingDaySchedule * {
  const auto found = std::find_if(schedules_.begin(), schedules_.end(),
                                  [trading_day](const auto &schedule) {
                                    return schedule.trading_day == trading_day;
                                  });
  return found == schedules_.end() ? nullptr : &*found;
}

}  // namespace Exchange
