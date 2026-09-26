#include "runtime/exchange_runtime.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simex::runtime {

ExchangeRuntime::ExchangeRuntime(ExchangeRuntimeConfig config)
    : config_(std::move(config)),
      harness_(config_.instrument, config_.queue_capacity),
      commands_(config_.queue_capacity),
      responses_(config_.queue_capacity),
      updates_(config_.queue_capacity) {}

ExchangeRuntime::~ExchangeRuntime() { stop(); }

auto ExchangeRuntime::start() -> bool {
  RuntimeState expected = RuntimeState::CREATED;
  if (!state_.compare_exchange_strong(expected, RuntimeState::STARTING)) return false;
  stop_requested_.store(false);
  thread_ = std::thread([this] { run(); });
  while (state_.load() == RuntimeState::STARTING) std::this_thread::yield();
  return ready();
}

auto ExchangeRuntime::stop() -> void {
  const auto current = state_.load();
  if (current == RuntimeState::CREATED || current == RuntimeState::STOPPED) return;
  stop_requested_.store(true);
  if (thread_.joinable()) thread_.join();
  if (state() != RuntimeState::FAILED) state_.store(RuntimeState::STOPPED);
}

auto ExchangeRuntime::submit(const simex::exchange::ClientRequest &request) -> bool {
  if (!ready()) return false;
  return commands_.tryPush(Command{Command::Type::REQUEST, request, 0});
}

auto ExchangeRuntime::advanceTo(simex::common::Nanos timestamp) -> bool {
  if (!ready() || config_.clock_mode == ClockMode::REALTIME) return false;
  return commands_.tryPush(Command{Command::Type::ADVANCE_TO, {}, timestamp});
}

auto ExchangeRuntime::advanceBy(simex::common::Nanos delta) -> bool {
  if (!ready() || config_.clock_mode == ClockMode::REALTIME) return false;
  return commands_.tryPush(Command{Command::Type::ADVANCE_BY, {}, delta});
}

auto ExchangeRuntime::requestSnapshot() -> bool {
  if (!ready()) return false;
  return commands_.tryPush(Command{Command::Type::SNAPSHOT, {}, 0});
}

auto ExchangeRuntime::initialize() -> bool {
  harness_.resetClock(config_.initial_time, config_.trading_day);
  current_time_ = config_.initial_time;
  if (!harness_.setReferencePrice(config_.reference_price) ||
      !harness_.setPhase(config_.initial_phase)) return false;
  for (const auto &event : config_.sessions) {
    if (!harness_.scheduleSessionPhase(event.timestamp, event.trading_day, event.phase)) return false;
  }
  for (const auto &event : config_.rollovers) {
    if (!harness_.scheduleTradingDayRollover(event.timestamp, event.trading_day)) return false;
  }
  realtime_origin_ = std::chrono::steady_clock::now();
  return true;
}

auto ExchangeRuntime::handle(const Command &command) -> bool {
  switch (command.type) {
    case Command::Type::REQUEST: {
      auto request = command.request;
      if (config_.clock_mode == ClockMode::REALTIME) {
        if (!advanceRealtime()) return false;
        request.rx_time = current_time_;
      }
      if (!harness_.submit(request)) return false;
      break;
    }
    case Command::Type::ADVANCE_TO:
      if (!harness_.advanceTo(command.timestamp)) return false;
      current_time_ = command.timestamp;
      break;
    case Command::Type::ADVANCE_BY:
      if (command.timestamp < 0 || current_time_ > std::numeric_limits<simex::common::Nanos>::max() - command.timestamp ||
          !harness_.advanceTo(current_time_ + command.timestamp)) return false;
      current_time_ += command.timestamp;
      break;
    case Command::Type::SNAPSHOT:
      break;
    case Command::Type::STOP:
      return false;
  }
  drainHarnessOutputs();
  return true;
}

auto ExchangeRuntime::advanceRealtime() -> bool {
  const auto elapsed = std::chrono::steady_clock::now() - realtime_origin_;
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  current_time_ = config_.initial_time + nanos;
  return harness_.advanceTo(current_time_);
}

auto ExchangeRuntime::drainHarnessOutputs() -> void {
  for (auto response : harness_.drainResponses()) {
    if (!responses_.tryPush(std::move(response))) {
      throw std::runtime_error("Runtime response queue overflow");
    }
  }
  for (auto update : harness_.drainUpdates()) {
    std::lock_guard lock(snapshot_mutex_);
    if (!updates_.tryPush(update)) throw std::runtime_error("Runtime market queue overflow");
    snapshot_synthesizer_.apply({++market_sequence_, update});
  }
}

auto ExchangeRuntime::run() -> void {
  try {
  if (!initialize()) {
    state_.store(RuntimeState::FAILED);
    return;
  }
  state_.store(RuntimeState::READY);
  while (!stop_requested_.load()) {
    Command command;
    bool did_work = false;
    while (commands_.tryPop(&command)) {
      did_work = true;
      if (!handle(command)) {
        state_.store(RuntimeState::FAILED);
        stop_requested_.store(true);
        break;
      }
    }
    if (config_.clock_mode == ClockMode::REALTIME) {
      if (!advanceRealtime()) {
        state_.store(RuntimeState::FAILED);
        break;
      }
      drainHarnessOutputs();
    }
    if (!did_work) std::this_thread::sleep_for(config_.realtime_poll);
  }
  drainHarnessOutputs();
  } catch (...) {
    state_.store(RuntimeState::FAILED);
    stop_requested_.store(true);
  }
}

}  // namespace simex::runtime
