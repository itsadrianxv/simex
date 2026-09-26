#pragma once

#include <cstdint>
#include <vector>

#include "common/lf_queue.h"
#include "exchange/messages.h"

namespace simex::exchange {

struct PublicMarketUpdate final {
  std::uint64_t sequence = 0;
  MarketUpdate update;
};

class MarketDataPublisher final {
 public:
  explicit MarketDataPublisher(MarketUpdateQueue *market_updates);

  auto publishPending() -> std::vector<PublicMarketUpdate>;
  [[nodiscard]] auto nextSequence() const noexcept -> std::uint64_t {
    return next_sequence_;
  }

 private:
  MarketUpdateQueue *market_updates_ = nullptr;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace simex::exchange
