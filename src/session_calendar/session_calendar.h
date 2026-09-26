#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "common/config.h"
#include "common/types.h"
#include "common/virtual_clock.h"

namespace simex::exchange {

struct TradingDaySchedule final {
  simex::common::TradingDayId trading_day = 0;
  simex::common::PriceTicks previous_settlement_ticks = simex::common::INVALID_PRICE_TICKS;
  bool night_session_enabled = true;
  std::optional<simex::common::TradingDayId> night_session_date;
};

struct SessionTransition final {
  simex::common::Nanos timestamp = 0;
  simex::common::TradingDayId trading_day = 0;
  simex::common::SessionPhase phase = simex::common::SessionPhase::CLOSED;
};

/// Maps explicit trading-day schedules onto Asia/Shanghai virtual timestamps.
class SessionCalendar final {
 public:
  explicit SessionCalendar(simex::common::SessionTemplateConfig template_config);

  auto addTradingDay(const TradingDaySchedule &schedule) -> bool;
  auto scheduleInto(simex::common::VirtualClock &clock,
                    simex::common::TradingDayId trading_day) const -> bool;
  auto scheduleRollover(simex::common::VirtualClock &clock, simex::common::Nanos timestamp,
                        simex::common::TradingDayId next_trading_day) const -> bool;
  auto clear() noexcept -> void;

  [[nodiscard]] auto phaseAt(simex::common::Nanos timestamp) const noexcept
      -> simex::common::SessionPhase;
  [[nodiscard]] auto nextTransitionAfter(simex::common::Nanos timestamp) const
      -> std::optional<SessionTransition>;
  [[nodiscard]] auto scheduleFor(simex::common::TradingDayId trading_day) const
      -> const TradingDaySchedule *;

  struct Interval final {
    simex::common::Nanos start = 0;
    simex::common::Nanos end = 0;
    simex::common::TradingDayId trading_day = 0;
    simex::common::SessionPhase phase = simex::common::SessionPhase::CLOSED;
  };

 private:

  simex::common::SessionTemplateConfig template_config_;
  std::vector<TradingDaySchedule> schedules_;
  std::vector<Interval> intervals_;
};

}  // namespace simex::exchange
