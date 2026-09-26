#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "common/order_types.h"

namespace simex::common {

/// A deterministic clock for replay and scenario-driven venue tests.
///
/// VirtualClock never reads wall-clock time and never sleeps.  Callers move it
/// forward explicitly with advanceTo()/advanceBy().  Trading-day boundaries
/// are supplied by the caller because an exchange calendar, including an SHFE
/// night session, is not a fixed 24-hour interval.
class VirtualClock final {
 public:
  using Nanos = std::int64_t;
  using TradingDayId = std::uint32_t;

  enum class EventType : std::uint8_t {
    TRADING_DAY_ROLLOVER,
    SESSION_PHASE,
  };

  struct Event final {
    EventType type = EventType::TRADING_DAY_ROLLOVER;
    Nanos timestamp = 0;
    TradingDayId trading_day = 0;
    SessionPhase phase = SessionPhase::CLOSED;
  };

  struct Rollover final {
    Nanos timestamp = 0;
    TradingDayId trading_day = 0;
  };

  explicit VirtualClock(Nanos initial_time = 0,
                        TradingDayId initial_trading_day = 0) noexcept
      : now_(initial_time), trading_day_(initial_trading_day) {}

  VirtualClock(const VirtualClock &) = delete;
  VirtualClock(VirtualClock &&) = delete;
  auto operator=(const VirtualClock &) -> VirtualClock & = delete;
  auto operator=(VirtualClock &&) -> VirtualClock & = delete;

  /// Return the current virtual timestamp in nanoseconds.
  [[nodiscard]] auto now() const noexcept -> Nanos { return now_; }

  /// Return the trading-day identifier currently active at now().
  [[nodiscard]] auto tradingDay() const noexcept -> TradingDayId {
    return trading_day_;
  }

  /// Reset a scenario before it starts.  This clears all scheduled rollovers.
  auto reset(Nanos timestamp, TradingDayId trading_day) noexcept -> void {
    now_ = timestamp;
    trading_day_ = trading_day;
    rollovers_.clear();
    next_rollover_ = 0;
    phase_events_.clear();
    next_phase_event_ = 0;
  }

  /// Set the clock during setup without synthesizing events.
  ///
  /// Operational time movement should use advanceTo()/advanceBy().  This
  /// method refuses to skip a scheduled rollover or to move backwards.
  [[nodiscard]] auto setTime(Nanos timestamp) noexcept -> bool {
    if (timestamp < now_) {
      return false;
    }

    if (next_rollover_ < rollovers_.size() &&
        rollovers_[next_rollover_].timestamp <= timestamp) {
      return false;
    }
    if (next_phase_event_ < phase_events_.size() &&
        phase_events_[next_phase_event_].timestamp <= timestamp) {
      return false;
    }

    now_ = timestamp;
    return true;
  }

  /// Schedule the next trading-day boundary.
  ///
  /// Rollovers must be registered in strictly increasing timestamp and
  /// trading-day order.  The schedule is intentionally explicit so holidays
  /// and SHFE night-session calendar rules stay outside this common component.
  [[nodiscard]] auto scheduleTradingDayRollover(
      Nanos timestamp, TradingDayId next_trading_day) -> bool {
    if (timestamp <= now_ || next_trading_day <= trading_day_) {
      return false;
    }

    if (!rollovers_.empty()) {
      const auto &last = rollovers_.back();
      if (timestamp <= last.timestamp ||
          next_trading_day <= last.trading_day) {
        return false;
      }
    }

    rollovers_.push_back({timestamp, next_trading_day});
    return true;
  }

  /// Schedule a phase transition supplied by an instrument session calendar.
  [[nodiscard]] auto scheduleSessionPhase(Nanos timestamp, TradingDayId trading_day,
                                           SessionPhase phase) -> bool {
    if (timestamp <= now_ || trading_day == 0) return false;
    if (!phase_events_.empty() && timestamp <= phase_events_.back().timestamp) return false;
    phase_events_.push_back({timestamp, trading_day, phase});
    return true;
  }

  /// Return how many rollover events have not yet been emitted.
  [[nodiscard]] auto pendingRolloverCount() const noexcept -> std::size_t {
    return rollovers_.size() - next_rollover_;
  }

  /// Move the clock forward and emit each crossed rollover in order.
  ///
  /// The callback is invoked after the clock has changed to the rollover
  /// timestamp and trading day.  A target equal to a rollover timestamp emits
  /// that rollover.  A backwards move returns false and leaves the clock
  /// unchanged.
  template <typename EventSink>
  [[nodiscard]] auto advanceTo(Nanos target, EventSink &&emit) -> bool {
    if (target < now_) {
      return false;
    }

    while (true) {
      const auto has_rollover = next_rollover_ < rollovers_.size() &&
                                rollovers_[next_rollover_].timestamp <= target;
      const auto has_phase = next_phase_event_ < phase_events_.size() &&
                             phase_events_[next_phase_event_].timestamp <= target;
      if (!has_rollover && !has_phase) break;

      if (has_phase && (!has_rollover ||
                        phase_events_[next_phase_event_].timestamp <=
                            rollovers_[next_rollover_].timestamp)) {
        const auto phase_event = phase_events_[next_phase_event_++];
        now_ = phase_event.timestamp;
        trading_day_ = phase_event.trading_day;
        emit(Event{EventType::SESSION_PHASE, now_, trading_day_, phase_event.phase});
      } else {
        const auto rollover = rollovers_[next_rollover_++];
        now_ = rollover.timestamp;
        trading_day_ = rollover.trading_day;
        emit(Event{EventType::TRADING_DAY_ROLLOVER, now_, trading_day_, SessionPhase::CLOSED});
      }
    }

    now_ = target;
    return true;
  }

  /// Move the clock forward by a non-negative number of nanoseconds.
  template <typename EventSink>
  [[nodiscard]] auto advanceBy(Nanos delta, EventSink &&emit) -> bool {
    if (delta < 0 || now_ > std::numeric_limits<Nanos>::max() - delta) {
      return false;
    }

    return advanceTo(now_ + delta, std::forward<EventSink>(emit));
  }

 private:
  Nanos now_ = 0;
  TradingDayId trading_day_ = 0;
  std::vector<Rollover> rollovers_;
  std::size_t next_rollover_ = 0;
  struct PhaseEvent final {
    Nanos timestamp = 0;
    TradingDayId trading_day = 0;
    SessionPhase phase = SessionPhase::CLOSED;
  };
  std::vector<PhaseEvent> phase_events_;
  std::size_t next_phase_event_ = 0;
};

}  // namespace simex::common
