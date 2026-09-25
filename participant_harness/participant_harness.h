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

namespace Harness {

class ParticipantHarness final {
 public:
  explicit ParticipantHarness(Common::InstrumentConfig instrument_config,
                              std::size_t queue_capacity = 4096);

  auto setReferencePrice(Common::PriceTicks previous_settlement_ticks) -> bool;
  auto setPhase(Common::SessionPhase phase) -> bool;
  auto receive(const Exchange::ClientRequest &request) -> bool;
  auto flush() -> bool;
  auto submit(const Exchange::ClientRequest &request) -> bool;
  auto pump() -> bool;
  auto scheduleTradingDayRollover(Common::Nanos timestamp,
                                  Common::TradingDayId next_trading_day) -> bool;
  auto scheduleSession(const Exchange::SessionCalendar &calendar,
                       Common::TradingDayId trading_day) -> bool;
  auto resetClock(Common::Nanos timestamp, Common::TradingDayId trading_day) -> void;
  auto advanceTo(Common::Nanos timestamp) -> bool;

  auto drainResponses() -> std::vector<Exchange::ClientResponse>;
  auto drainUpdates() -> std::vector<Exchange::MarketUpdate>;
  [[nodiscard]] auto stateHash() const -> std::string;
  [[nodiscard]] auto traceRecord() const -> Trace::TraceRecord;
  auto saveTrace(const std::filesystem::path &path) const -> void;
  auto replayTrace(const Trace::TraceRecord &expected) const -> bool;
  [[nodiscard]] auto clearing() const noexcept -> const Exchange::FrontClearing & {
    return clearing_;
  }

  auto saveTrace(const std::filesystem::path &path, const Trace::TraceRecord &record) const -> void;
  auto loadTrace(const std::filesystem::path &path) const -> Trace::TraceRecord;

 private:
  Common::InstrumentConfig instrument_config_;
  std::size_t queue_capacity_ = 0;
  Common::LFQueue<Exchange::ClientRequest> requests_;
  Exchange::ClientResponseQueue responses_;
  Exchange::MarketUpdateQueue updates_;
  Exchange::FrontClearing clearing_;
  Exchange::MatchingEngine matching_engine_;
  Exchange::FIFOSequencer sequencer_;
  Common::VirtualClock clock_;
  Trace::TraceRecord trace_record_;
};

}  // namespace Harness
