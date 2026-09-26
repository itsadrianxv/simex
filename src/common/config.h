#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "common/order_types.h"
#include "common/types.h"

namespace simex::common {

struct SessionWindowConfig final {
  SessionPhase phase = SessionPhase::CLOSED;
  int start_minute = 0;
  int end_minute = 0;
  bool crosses_midnight = false;
};

struct SessionTemplateConfig final {
  std::vector<SessionWindowConfig> night;
  std::vector<SessionWindowConfig> day;
  SessionWindowConfig overnight_closed;
  int daily_close_minute = 15 * 60;
};

struct OrderCombination final {
  OrderType order_type = OrderType::LIMIT;
  TimeInForce time_in_force = TimeInForce::DAY;
};

struct InstrumentConfig final {
  std::string symbol;
  std::string profile;
  std::string exchange;
  Qty contract_size_tons = 0;
  PriceTicks tick_size = 0;
  double price_limit_percent = 0.0;
  std::size_t max_price_levels = 0;
  PriceTicks price_index_base_offset = 0;
  std::vector<OrderCombination> supported_order_combinations;
  std::vector<OrderType> auction_allowed_order_types;
  std::vector<TimeInForce> auction_allowed_time_in_force;
};

struct SimexConfig final {
  int schema_version = 0;
  std::string timezone;
  InstrumentConfig instrument;
  SessionTemplateConfig session_template;
};

/// Load and validate the root simex JSON configuration.
/// Throws std::runtime_error for malformed or unsupported configuration.
auto loadSimexConfig(const std::filesystem::path &path) -> SimexConfig;

}  // namespace simex::common
