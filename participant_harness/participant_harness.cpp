#include "participant_harness/participant_harness.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Harness {

ParticipantHarness::ParticipantHarness(Common::InstrumentConfig instrument_config,
                                       std::size_t queue_capacity)
    : instrument_config_(std::move(instrument_config)),
      queue_capacity_(queue_capacity),
      requests_(queue_capacity),
      responses_(queue_capacity),
      updates_(queue_capacity),
      matching_engine_(instrument_config_, &clearing_, &responses_, &updates_),
      sequencer_(&requests_),
      clock_(0, 0) {
  trace_record_.schema_version = 1;
  trace_record_.scenario = "participant_harness";
  trace_record_.config = "simex.json";
}

auto ParticipantHarness::setReferencePrice(Common::PriceTicks previous_settlement_ticks) -> bool {
  trace_record_.previous_settlement_ticks = previous_settlement_ticks;
  return matching_engine_.setReferencePrice(previous_settlement_ticks);
}

auto ParticipantHarness::setPhase(Common::SessionPhase phase) -> bool {
  if (trace_record_.requests.empty()) trace_record_.initial_phase = phase;
  return matching_engine_.setPhase(phase);
}

auto ParticipantHarness::receive(const Exchange::ClientRequest &request) -> bool {
  if (!sequencer_.addClientRequest(request)) return false;
  trace_record_.requests.push_back(request);
  return true;
}

auto ParticipantHarness::flush() -> bool {
  return sequencer_.sequenceAndPublish() && pump();
}

auto ParticipantHarness::submit(const Exchange::ClientRequest &request) -> bool {
  return receive(request) && flush();
}

auto ParticipantHarness::pump() -> bool {
  Exchange::ClientRequest request;
  while (requests_.tryPop(&request)) {
    if (!matching_engine_.processClientRequest(request)) return false;
  }
  return true;
}

auto ParticipantHarness::scheduleTradingDayRollover(Common::Nanos timestamp,
                                                     Common::TradingDayId next_trading_day) -> bool {
  if (!clock_.scheduleTradingDayRollover(timestamp, next_trading_day)) return false;
  trace_record_.rollovers.push_back({timestamp, next_trading_day});
  return true;
}

auto ParticipantHarness::scheduleSession(const Exchange::SessionCalendar &calendar,
                                         Common::TradingDayId trading_day) -> bool {
  return calendar.scheduleInto(clock_, trading_day);
}

auto ParticipantHarness::resetClock(Common::Nanos timestamp,
                                    Common::TradingDayId trading_day) -> void {
  clock_.reset(timestamp, trading_day);
  trace_record_.initial_trading_day = trading_day;
}

auto ParticipantHarness::advanceTo(Common::Nanos timestamp) -> bool {
  try {
    return clock_.advanceTo(timestamp, [this](const auto &event) {
      if (event.type == Common::VirtualClock::EventType::SESSION_PHASE) {
        if (!matching_engine_.setPhase(event.phase)) {
          throw std::logic_error("session phase transition failed");
        }
      } else if (!matching_engine_.onTradingDayRollover()) {
        throw std::logic_error("trading-day rollover failed");
      }
    });
  } catch (const std::logic_error &) {
    return false;
  }
}

auto ParticipantHarness::drainResponses() -> std::vector<Exchange::ClientResponse> {
  std::vector<Exchange::ClientResponse> result;
  Exchange::ClientResponse response;
  while (responses_.tryPop(&response)) {
    result.push_back(response);
    trace_record_.private_responses.push_back(response);
  }
  return result;
}

auto ParticipantHarness::drainUpdates() -> std::vector<Exchange::MarketUpdate> {
  std::vector<Exchange::MarketUpdate> result;
  Exchange::MarketUpdate update;
  while (updates_.tryPop(&update)) {
    result.push_back(update);
    trace_record_.public_updates.push_back(update);
  }
  return result;
}

auto ParticipantHarness::stateHash() const -> std::string {
  return Trace::sha256(matching_engine_.canonicalState());
}

auto ParticipantHarness::traceRecord() const -> Trace::TraceRecord {
  auto record = trace_record_;
  std::stable_sort(record.requests.begin(), record.requests.end(), [](const auto &left, const auto &right) {
    return left.rx_time < right.rx_time;
  });
  std::sort(record.rollovers.begin(), record.rollovers.end(), [](const auto &left, const auto &right) {
    return left.timestamp < right.timestamp;
  });
  record.final_state_hash = stateHash();
  return record;
}

auto ParticipantHarness::saveTrace(const std::filesystem::path &path) const -> void {
  Trace::write(path, traceRecord());
}

auto ParticipantHarness::replayTrace(const Trace::TraceRecord &expected) const -> bool {
  ParticipantHarness replay(instrument_config_, queue_capacity_);
  replay.trace_record_.scenario = expected.scenario;
  replay.trace_record_.config = expected.config;
  replay.resetClock(0, expected.initial_trading_day);
  if (expected.previous_settlement_ticks != Common::INVALID_PRICE_TICKS &&
      !replay.setReferencePrice(expected.previous_settlement_ticks)) {
    return false;
  }
  if (!replay.setPhase(expected.initial_phase)) return false;
  for (const auto &rollover : expected.rollovers) {
    if (!replay.scheduleTradingDayRollover(rollover.timestamp, rollover.trading_day)) return false;
  }

  auto requests = expected.requests;
  std::stable_sort(requests.begin(), requests.end(), [](const auto &left, const auto &right) {
    return left.rx_time < right.rx_time;
  });
  std::size_t rollover_index = 0;
  for (const auto &request : requests) {
    while (rollover_index < expected.rollovers.size() &&
           expected.rollovers[rollover_index].timestamp <= request.rx_time) {
      if (!replay.flush() || !replay.advanceTo(expected.rollovers[rollover_index].timestamp)) return false;
      ++rollover_index;
    }
    if (!replay.receive(request)) return false;
  }
  if (!replay.flush()) return false;
  replay.drainResponses();
  replay.drainUpdates();
  while (rollover_index < expected.rollovers.size()) {
    if (!replay.flush() || !replay.advanceTo(expected.rollovers[rollover_index].timestamp)) return false;
    ++rollover_index;
  }
  replay.drainResponses();
  replay.drainUpdates();
  const auto actual = replay.traceRecord();
  auto normalized_expected = expected;
  std::stable_sort(normalized_expected.requests.begin(), normalized_expected.requests.end(),
                   [](const auto &left, const auto &right) { return left.rx_time < right.rx_time; });
  std::sort(normalized_expected.rollovers.begin(), normalized_expected.rollovers.end(),
            [](const auto &left, const auto &right) { return left.timestamp < right.timestamp; });
  return actual.initial_trading_day == normalized_expected.initial_trading_day &&
         actual.previous_settlement_ticks == normalized_expected.previous_settlement_ticks &&
         actual.initial_phase == normalized_expected.initial_phase &&
         actual.requests == normalized_expected.requests &&
         actual.rollovers == normalized_expected.rollovers &&
         actual.final_state_hash == expected.final_state_hash &&
         actual.private_responses == expected.private_responses &&
         actual.public_updates == expected.public_updates;
}

auto ParticipantHarness::saveTrace(const std::filesystem::path &path,
                                   const Trace::TraceRecord &record) const -> void {
  Trace::write(path, record);
}

auto ParticipantHarness::loadTrace(const std::filesystem::path &path) const -> Trace::TraceRecord {
  return Trace::read(path);
}

}  // namespace Harness
