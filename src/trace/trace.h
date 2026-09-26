#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "common/types.h"
#include "exchange/messages.h"

namespace simex::trace {

struct Rollover final {
  simex::common::Nanos timestamp = 0;
  simex::common::TradingDayId trading_day = 0;
  auto operator==(const Rollover &) const noexcept -> bool = default;
};

struct TraceRecord final {
  int schema_version = 1;
  std::string scenario;
  std::string config;
  simex::common::TradingDayId initial_trading_day = 0;
  simex::common::PriceTicks previous_settlement_ticks = simex::common::INVALID_PRICE_TICKS;
  simex::common::SessionPhase initial_phase = simex::common::SessionPhase::CLOSED;
  std::vector<simex::exchange::ClientRequest> requests;
  std::vector<Rollover> rollovers;
  std::vector<simex::exchange::ClientResponse> private_responses;
  std::vector<simex::exchange::MarketUpdate> public_updates;
  std::string final_state_hash;
};

auto write(const std::filesystem::path &path, const TraceRecord &record) -> void;
auto read(const std::filesystem::path &path) -> TraceRecord;
auto sha256(std::string_view canonical_bytes) -> std::string;

}  // namespace simex::trace
