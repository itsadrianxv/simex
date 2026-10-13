#include <cassert>

#include "market_data/market_data_publisher.h"
#include "market_data/snapshot_synthesizer.h"

int main() {
  simex::exchange::MarketUpdateQueue updates(8);
  simex::exchange::MarketDataPublisher publisher(&updates);
  simex::exchange::SnapshotSynthesizer snapshot;

  assert(updates.tryPush({simex::common::MarketUpdateType::ADD, 0, 7, simex::common::Side::BUY,
                          1000, 3, 3, 1}));
  auto incremental = publisher.publishPending();
  assert(incremental.size() == 1);
  assert(incremental.front().sequence == 1);
  snapshot.apply(incremental.front());
  assert(snapshot.synthesize().orders.size() == 1);

  assert(updates.tryPush({simex::common::MarketUpdateType::MODIFY, 0, 7, simex::common::Side::BUY,
                          1000, 1, 1, 2}));
  incremental = publisher.publishPending();
  snapshot.apply(incremental.front());
  auto current = snapshot.synthesize();
  assert(current.last_incremental_sequence == 2);
  assert(current.orders.front().update.qty == 1);

  assert(updates.tryPush({simex::common::MarketUpdateType::CANCEL, 0, 7, simex::common::Side::BUY,
                          1000, 1, 0, 3}));
  incremental = publisher.publishPending();
  snapshot.apply(incremental.front());
  assert(snapshot.synthesize().orders.empty());
  return 0;
}
