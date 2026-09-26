#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <chrono>
#include "exchange/messages.h"
#include "market_data/snapshot_synthesizer.h"
namespace simex::transport {
class UdpMarketDataPublisher final {
 public:
  using UpdateSource = std::function<bool(simex::exchange::MarketUpdate *, std::uint64_t *)>;
  using SnapshotSource = std::function<bool(simex::exchange::Snapshot *)>;
  using LegacySnapshotSource = std::function<bool(std::uint64_t *)>;
  UdpMarketDataPublisher(std::uint16_t port, std::chrono::milliseconds snapshot_interval = std::chrono::seconds(1));
  ~UdpMarketDataPublisher();
  auto start(UpdateSource updates, SnapshotSource snapshots) -> bool;
  // Compatibility overload for the initial header-only snapshot source.
  auto start(UpdateSource updates, LegacySnapshotSource snapshots) -> bool;
  auto stop() -> void;
 private:
  void run();
  std::uint16_t port_;
  std::chrono::milliseconds snapshot_interval_;
  UpdateSource updates_;
  SnapshotSource snapshots_;
  LegacySnapshotSource legacy_snapshots_;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
  int socket_ = -1;
};
}  // namespace simex::transport
