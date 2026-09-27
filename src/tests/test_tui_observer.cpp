#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "transport/protocol.h"
#include "tui/market_observer.h"

static auto sendPacket(int fd, std::uint16_t port, const void *data, std::size_t size) -> void {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  assert(::sendto(fd, data, size, 0, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
         static_cast<ssize_t>(size));
}

static auto waitFor(const simex::tui::MarketObserver &observer,
                    simex::tui::ObserverState state) -> simex::tui::ObserverView {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto view = observer.view();
    if (view.state == state) return view;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return observer.view();
}

static auto waitForSequence(const simex::tui::MarketObserver &observer, std::uint64_t sequence)
    -> simex::tui::ObserverView {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto view = observer.view();
    if (view.sequence == sequence) return view;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return observer.view();
}

int main() {
  constexpr std::uint16_t port = 29302;
  simex::tui::MarketObserver observer(port);
  assert(observer.start());
  const auto fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd >= 0);

  simex::transport::WireSnapshotPrefix snapshot{};
  snapshot.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::SNAPSHOT);
  snapshot.header.sequence = 4;
  snapshot.order_count = 1;
  snapshot.header.payload_size = sizeof(snapshot.order_count) + sizeof(snapshot.reserved) + sizeof(simex::transport::WireSnapshotOrder);
  simex::transport::WireSnapshotOrder order{};
  order.sequence = 4;
  order.update.type = simex::common::MarketUpdateType::ADD;
  order.update.market_order_id = 7;
  order.update.side = simex::common::Side::BUY;
  order.update.price_ticks = 3499;
  order.update.qty = 2;
  order.update.leaves_qty = 2;
  std::vector<std::byte> snapshot_packet(sizeof(snapshot) + sizeof(order));
  std::memcpy(snapshot_packet.data(), &snapshot, sizeof(snapshot));
  std::memcpy(snapshot_packet.data() + sizeof(snapshot), &order, sizeof(order));
  sendPacket(fd, port, snapshot_packet.data(), snapshot_packet.size());
  auto view = waitFor(observer, simex::tui::ObserverState::SYNCED);
  assert(view.state == simex::tui::ObserverState::SYNCED && view.sequence == 4);
  assert(view.bids.size() == 1 && view.bids.front().quantity == 2);

  simex::transport::WireUpdate update{};
  update.header.kind = static_cast<std::uint8_t>(simex::transport::MessageKind::UPDATE);
  update.header.payload_size = sizeof(update.update);
  update.header.sequence = 5;
  update.update.type = simex::common::MarketUpdateType::TRADE;
  update.update.price_ticks = 3500;
  update.update.qty = 1;
  sendPacket(fd, port, &update, sizeof(update));
  view = waitForSequence(observer, 5);
  assert(view.sequence == 5 && view.has_trade && view.last_trade_price == 3500);
  assert(view.bids.front().quantity == 2);

  update.header.sequence = 7;
  update.update.type = simex::common::MarketUpdateType::CANCEL;
  update.update.market_order_id = 7;
  sendPacket(fd, port, &update, sizeof(update));
  view = waitFor(observer, simex::tui::ObserverState::GAP);
  assert(view.state == simex::tui::ObserverState::GAP && view.sequence == 5);
  assert(view.bids.front().quantity == 2);

  snapshot.header.sequence = 7;
  std::memcpy(snapshot_packet.data(), &snapshot, sizeof(snapshot));
  sendPacket(fd, port, snapshot_packet.data(), snapshot_packet.size());
  view = waitFor(observer, simex::tui::ObserverState::SYNCED);
  assert(view.state == simex::tui::ObserverState::SYNCED && view.sequence == 7);

  ::close(fd);
  observer.stop();
  return 0;
}
