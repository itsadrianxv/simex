#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "common/lf_queue.h"
#include "common/types.h"
#include "exchange/messages.h"
#include "front_clearing/front_clearing.h"
#include "matching_engine/matching_engine.h"
#include "order_server/fifo_sequencer.h"
#include "session_calendar/session_calendar.h"
#include "trace/trace.h"

namespace simex::harness {

class ParticipantHarness final {
 public:
  explicit ParticipantHarness(simex::common::InstrumentConfig instrument_config,
                              std::size_t queue_capacity = 4096);

  auto setReferencePrice(simex::common::PriceTicks previous_settlement_ticks) -> bool;
  auto setPhase(simex::common::SessionPhase phase) -> bool;
  auto receive(const simex::exchange::ClientRequest &request) -> bool;
  auto flush() -> bool;
  auto submit(const simex::exchange::ClientRequest &request) -> bool;
  auto pump() -> bool;
  auto scheduleTradingDayRollover(simex::common::Nanos timestamp,
                                  simex::common::TradingDayId next_trading_day) -> bool;
  auto scheduleSession(const simex::exchange::SessionCalendar &calendar,
                       simex::common::TradingDayId trading_day) -> bool;
  auto scheduleSessionPhase(simex::common::Nanos timestamp,
                            simex::common::TradingDayId trading_day,
                            simex::common::SessionPhase phase) -> bool;
  auto resetClock(simex::common::Nanos timestamp, simex::common::TradingDayId trading_day) -> void;
  auto advanceTo(simex::common::Nanos timestamp) -> bool;

  auto drainResponses() -> std::vector<simex::exchange::ClientResponse>;
  auto drainUpdates() -> std::vector<simex::exchange::MarketUpdate>;
  [[nodiscard]] auto stateHash() const -> std::string;
  [[nodiscard]] auto traceRecord() const -> simex::trace::TraceRecord;
  auto saveTrace(const std::filesystem::path &path) const -> void;
  auto replayTrace(const simex::trace::TraceRecord &expected) const -> bool;
  [[nodiscard]] auto clearing() const noexcept -> const simex::exchange::FrontClearing & {
    return clearing_;
  }

  auto saveTrace(const std::filesystem::path &path, const simex::trace::TraceRecord &record) const -> void;
  auto loadTrace(const std::filesystem::path &path) const -> simex::trace::TraceRecord;

 private:
  simex::common::InstrumentConfig instrument_config_;
  std::size_t queue_capacity_ = 0;
  simex::common::LFQueue<simex::exchange::ClientRequest> requests_;
  simex::exchange::ClientResponseQueue responses_;
  simex::exchange::MarketUpdateQueue updates_;
  simex::exchange::FrontClearing clearing_;
  simex::exchange::MatchingEngine matching_engine_;
  simex::exchange::FIFOSequencer sequencer_;
  simex::common::VirtualClock clock_;
  simex::trace::TraceRecord trace_record_;
};

}  // namespace simex::harness
