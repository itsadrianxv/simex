#include "market_data/market_data_publisher.h"

#include <stdexcept>

namespace simex::exchange {

MarketDataPublisher::MarketDataPublisher(MarketUpdateQueue *market_updates)
    : market_updates_(market_updates) {
  if (market_updates_ == nullptr) {
    throw std::invalid_argument("MarketDataPublisher requires a market update queue");
  }
}

auto MarketDataPublisher::publishPending() -> std::vector<PublicMarketUpdate> {
  std::vector<PublicMarketUpdate> result;
  MarketUpdate update;
  while (market_updates_->tryPop(&update)) {
    result.push_back({next_sequence_++, update});
  }
  return result;
}

}  // namespace simex::exchange
