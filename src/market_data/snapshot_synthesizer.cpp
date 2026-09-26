#include "market_data/snapshot_synthesizer.h"

#include <algorithm>

namespace simex::exchange {

auto SnapshotSynthesizer::apply(const PublicMarketUpdate &update) -> void {
  last_incremental_sequence_ = update.sequence;
  switch (update.update.type) {
    case simex::common::MarketUpdateType::ADD:
      live_orders_[update.update.market_order_id] = update;
      break;
    case simex::common::MarketUpdateType::MODIFY: {
      const auto found = live_orders_.find(update.update.market_order_id);
      if (found != live_orders_.end()) {
        found->second = update;
        found->second.update.qty = update.update.leaves_qty;
      }
      break;
    }
    case simex::common::MarketUpdateType::CANCEL:
      live_orders_.erase(update.update.market_order_id);
      break;
    case simex::common::MarketUpdateType::TRADE:
      break;
  }
}

auto SnapshotSynthesizer::synthesize() const -> Snapshot {
  Snapshot snapshot;
  snapshot.last_incremental_sequence = last_incremental_sequence_;
  snapshot.orders.reserve(live_orders_.size());
  for (const auto &[market_order_id, update] : live_orders_) {
    snapshot.orders.push_back(update);
  }
  std::sort(snapshot.orders.begin(), snapshot.orders.end(), [](const auto &left, const auto &right) {
    if (left.update.ticker_id != right.update.ticker_id) {
      return left.update.ticker_id < right.update.ticker_id;
    }
    if (left.update.side != right.update.side) {
      return left.update.side == simex::common::Side::SELL;
    }
    if (left.update.price_ticks != right.update.price_ticks) {
      return left.update.side == simex::common::Side::BUY
                 ? left.update.price_ticks > right.update.price_ticks
                 : left.update.price_ticks < right.update.price_ticks;
    }
    return left.update.market_order_id < right.update.market_order_id;
  });
  return snapshot;
}

}  // namespace simex::exchange
