#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "common/config.h"
#include "common/types.h"
#include "common/virtual_clock.h"

namespace Exchange {

struct TradingDaySchedule final {
  Common::TradingDayId trading_day = 0;
  Common::PriceTicks previous_settlement_ticks = Common::INVALID_PRICE_TICKS;
  bool night_session_enabled = true;
  std::optional<Common::TradingDayId> night_session_date;
};

struct SessionTransition final {
  Common::Nanos timestamp = 0;
  Common::TradingDayId trading_day = 0;
  Common::SessionPhase phase = Common::SessionPhase::CLOSED;
};

/// Maps explicit trading-day schedules onto Asia/Shanghai virtual timestamps.
class SessionCalendar final {
 public:
  explicit SessionCalendar(Common::SessionTemplateConfig template_config);

  auto addTradingDay(const TradingDaySchedule &schedule) -> bool;
  auto scheduleInto(Common::VirtualClock &clock,
                    Common::TradingDayId trading_day) const -> bool;
  auto scheduleRollover(Common::VirtualClock &clock, Common::Nanos timestamp,
                        Common::TradingDayId next_trading_day) const -> bool;
  auto clear() noexcept -> void;

  [[nodiscard]] auto phaseAt(Common::Nanos timestamp) const noexcept
      -> Common::SessionPhase;
  [[nodiscard]] auto nextTransitionAfter(Common::Nanos timestamp) const
      -> std::optional<SessionTransition>;
  [[nodiscard]] auto scheduleFor(Common::TradingDayId trading_day) const
      -> const TradingDaySchedule *;

  struct Interval final {
    Common::Nanos start = 0;
    Common::Nanos end = 0;
    Common::TradingDayId trading_day = 0;
    Common::SessionPhase phase = Common::SessionPhase::CLOSED;
  };

 private:

  Common::SessionTemplateConfig template_config_;
  std::vector<TradingDaySchedule> schedules_;
  std::vector<Interval> intervals_;
};

}  // namespace Exchange
