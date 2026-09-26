#include "transport/udp_market_data_publisher.h"
#include "transport/protocol.h"
#include <chrono>
#include <cstring>
#ifndef _WIN32
#include <arpa/inet.h>
#include <vector>
#include <cstddef>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace simex::transport {
UdpMarketDataPublisher::UdpMarketDataPublisher(std::uint16_t port, std::chrono::milliseconds interval) : port_(port), snapshot_interval_(interval) {}
UdpMarketDataPublisher::~UdpMarketDataPublisher() { stop(); }
auto UdpMarketDataPublisher::start(UpdateSource updates, SnapshotSource snapshots) -> bool {
#ifdef _WIN32
  (void)updates; (void)snapshots; return false;
#else
  if (thread_.joinable()) return false;
  updates_ = std::move(updates); snapshots_ = std::move(snapshots); legacy_snapshots_ = {};
  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ < 0) return false;
  stopping_.store(false); thread_ = std::thread([this] { run(); }); return true;
#endif
}
auto UdpMarketDataPublisher::start(UpdateSource updates, LegacySnapshotSource snapshots) -> bool {
#ifdef _WIN32
  (void)updates; (void)snapshots; return false;
#else
  if (thread_.joinable()) return false;
  updates_ = std::move(updates); snapshots_ = {}; legacy_snapshots_ = std::move(snapshots);
  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ < 0) return false;
  stopping_.store(false); thread_ = std::thread([this] { run(); }); return true;
#endif
}
auto UdpMarketDataPublisher::stop() -> void {
  stopping_.store(true);
  if (thread_.joinable()) thread_.join();
  if (socket_ >= 0) { ::close(socket_); socket_ = -1; }
}
void UdpMarketDataPublisher::run() {
#ifndef _WIN32
  sockaddr_in destination{}; destination.sin_family = AF_INET; destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK); destination.sin_port = htons(port_);
  auto next_snapshot = std::chrono::steady_clock::now();
  while (!stopping_.load()) {
    simex::exchange::MarketUpdate update; std::uint64_t sequence = 0;
    if (updates_ && updates_(&update, &sequence)) {
      WireUpdate wire{}; wire.header.kind = static_cast<std::uint8_t>(MessageKind::UPDATE); wire.header.payload_size = sizeof(wire.update); wire.header.sequence = sequence; wire.update = update;
      ::sendto(socket_, &wire, sizeof(wire), 0, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    }
    if (snapshot_interval_.count() > 0 && std::chrono::steady_clock::now() >= next_snapshot) {
      simex::exchange::Snapshot snapshot; bool available = false;
      if (snapshots_) available = snapshots_(&snapshot);
      else if (legacy_snapshots_) { std::uint64_t last = 0; available = legacy_snapshots_(&last); snapshot.last_incremental_sequence = last; }
      if (available) {
        WireSnapshotPrefix prefix{}; prefix.header.kind = static_cast<std::uint8_t>(MessageKind::SNAPSHOT); prefix.header.sequence = snapshot.last_incremental_sequence; prefix.order_count = static_cast<std::uint32_t>(snapshot.orders.size());
        prefix.header.payload_size = static_cast<std::uint32_t>(sizeof(prefix.order_count) + sizeof(prefix.reserved) + snapshot.orders.size() * sizeof(WireSnapshotOrder));
        std::vector<std::byte> packet(sizeof(prefix) + snapshot.orders.size() * sizeof(WireSnapshotOrder));
        std::memcpy(packet.data(), &prefix, sizeof(prefix));
        auto *orders = reinterpret_cast<WireSnapshotOrder *>(packet.data() + sizeof(prefix));
        for (std::size_t i = 0; i < snapshot.orders.size(); ++i) { orders[i].sequence = snapshot.orders[i].sequence; orders[i].update = snapshot.orders[i].update; }
        ::sendto(socket_, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
      }
      next_snapshot = std::chrono::steady_clock::now() + snapshot_interval_;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
#endif
}
}  // namespace simex::transport
