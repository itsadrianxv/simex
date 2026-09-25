#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "market_data/market_data_publisher.h"

namespace Exchange {

struct Snapshot final {
  std::uint64_t last_incremental_sequence = 0;
  std::vector<PublicMarketUpdate> orders;
};

class SnapshotSynthesizer final {
 public:
  auto apply(const PublicMarketUpdate &update) -> void;
  [[nodiscard]] auto synthesize() const -> Snapshot;

 private:
  std::uint64_t last_incremental_sequence_ = 0;
  std::unordered_map<Common::MarketOrderId, PublicMarketUpdate> live_orders_;
};

}  // namespace Exchange
