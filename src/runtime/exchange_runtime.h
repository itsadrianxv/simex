#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <limits>
#include <vector>

#include "common/lf_queue.h"
#include "common/types.h"
#include "exchange/messages.h"
#include "participant_harness/participant_harness.h"
#include "market_data/snapshot_synthesizer.h"

namespace simex::runtime {

enum class ClockMode : std::uint8_t { REALTIME, MANUAL, REPLAY };
enum class RuntimeState : std::uint8_t { CREATED, STARTING, READY, STOPPING, STOPPED, FAILED };

struct SessionEvent final {
  simex::common::Nanos timestamp = 0;
  simex::common::TradingDayId trading_day = 0;
  simex::common::SessionPhase phase = simex::common::SessionPhase::CLOSED;
};

struct RolloverEvent final {
  simex::common::Nanos timestamp = 0;
  simex::common::TradingDayId trading_day = 0;
};

struct ExchangeRuntimeConfig final {
  simex::common::InstrumentConfig instrument;
  simex::common::PriceTicks reference_price = simex::common::INVALID_PRICE_TICKS;
  simex::common::TradingDayId trading_day = 0;
  simex::common::Nanos initial_time = 0;
  simex::common::SessionPhase initial_phase = simex::common::SessionPhase::CLOSED;
  std::vector<SessionEvent> sessions;
  std::vector<RolloverEvent> rollovers;
  ClockMode clock_mode = ClockMode::MANUAL;
  std::size_t queue_capacity = 4096;
  std::chrono::milliseconds realtime_poll{1};
};

struct Command final {
  enum class Type : std::uint8_t { REQUEST, ADVANCE_TO, ADVANCE_BY, SNAPSHOT, STOP };
  Type type = Type::REQUEST;
  simex::exchange::ClientRequest request{};
  simex::common::Nanos timestamp = 0;
};

class ExchangeRuntime final {
 public:
  explicit ExchangeRuntime(ExchangeRuntimeConfig config);
  ~ExchangeRuntime();

  ExchangeRuntime(const ExchangeRuntime &) = delete;
  auto operator=(const ExchangeRuntime &) -> ExchangeRuntime & = delete;

  auto start() -> bool;
  auto stop() -> void;
  auto submit(const simex::exchange::ClientRequest &request) -> bool;
  auto advanceTo(simex::common::Nanos timestamp) -> bool;
  auto advanceBy(simex::common::Nanos delta) -> bool;
  auto requestSnapshot() -> bool;
  [[nodiscard]] auto snapshot() const -> simex::exchange::Snapshot { return snapshot_synthesizer_.synthesize(); }

  [[nodiscard]] auto ready() const noexcept -> bool { return state() == RuntimeState::READY; }
  [[nodiscard]] auto state() const noexcept -> RuntimeState { return state_.load(); }
  [[nodiscard]] auto responses() noexcept -> simex::exchange::ClientResponseQueue & { return responses_; }
  [[nodiscard]] auto updates() noexcept -> simex::exchange::MarketUpdateQueue & { return updates_; }
  [[nodiscard]] auto commands() noexcept -> simex::common::LFQueue<Command> & { return commands_; }

 private:
  auto run() -> void;
  auto initialize() -> bool;
  auto handle(const Command &command) -> bool;
  auto drainHarnessOutputs() -> void;
  auto advanceRealtime() -> bool;

  ExchangeRuntimeConfig config_;
  simex::harness::ParticipantHarness harness_;
  simex::common::LFQueue<Command> commands_;
  simex::exchange::ClientResponseQueue responses_;
  simex::exchange::MarketUpdateQueue updates_;
  simex::exchange::SnapshotSynthesizer snapshot_synthesizer_;
  std::atomic<RuntimeState> state_{RuntimeState::CREATED};
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  std::chrono::steady_clock::time_point realtime_origin_{};
  simex::common::Nanos current_time_ = 0;
};

}  // namespace simex::runtime
