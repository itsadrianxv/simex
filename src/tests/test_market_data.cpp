#include <cassert>

#include "market_data/market_data_publisher.h"
#include "market_data/snapshot_synthesizer.h"

int main() {
  Exchange::MarketUpdateQueue updates(8);
  Exchange::MarketDataPublisher publisher(&updates);
  Exchange::SnapshotSynthesizer snapshot;

  assert(updates.tryPush({Common::MarketUpdateType::ADD, 0, 7, Common::Side::BUY,
                          1000, 3, 3, 1}));
  auto incremental = publisher.publishPending();
  assert(incremental.size() == 1);
  assert(incremental.front().sequence == 1);
  snapshot.apply(incremental.front());
  assert(snapshot.synthesize().orders.size() == 1);

  assert(updates.tryPush({Common::MarketUpdateType::MODIFY, 0, 7, Common::Side::BUY,
                          1000, 1, 1, 2}));
  incremental = publisher.publishPending();
  snapshot.apply(incremental.front());
  auto current = snapshot.synthesize();
  assert(current.last_incremental_sequence == 2);
  assert(current.orders.front().update.qty == 1);

  assert(updates.tryPush({Common::MarketUpdateType::CANCEL, 0, 7, Common::Side::BUY,
                          1000, 1, 0, 3}));
  incremental = publisher.publishPending();
  snapshot.apply(incremental.front());
  assert(snapshot.synthesize().orders.empty());
  return 0;
}
