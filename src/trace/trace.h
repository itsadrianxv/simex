#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "common/types.h"
#include "exchange/messages.h"

namespace Trace {

struct Rollover final {
  Common::Nanos timestamp = 0;
  Common::TradingDayId trading_day = 0;
  auto operator==(const Rollover &) const noexcept -> bool = default;
};

struct TraceRecord final {
  int schema_version = 1;
  std::string scenario;
  std::string config;
  Common::TradingDayId initial_trading_day = 0;
  Common::PriceTicks previous_settlement_ticks = Common::INVALID_PRICE_TICKS;
  Common::SessionPhase initial_phase = Common::SessionPhase::CLOSED;
  std::vector<Exchange::ClientRequest> requests;
  std::vector<Rollover> rollovers;
  std::vector<Exchange::ClientResponse> private_responses;
  std::vector<Exchange::MarketUpdate> public_updates;
  std::string final_state_hash;
};

auto write(const std::filesystem::path &path, const TraceRecord &record) -> void;
auto read(const std::filesystem::path &path) -> TraceRecord;
auto sha256(std::string_view canonical_bytes) -> std::string;

}  // namespace Trace
