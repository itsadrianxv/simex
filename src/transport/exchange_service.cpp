#include "transport/exchange_service.h"
#include <thread>
#include <utility>
namespace simex::transport {
ExchangeService::ExchangeService(ExchangeServiceConfig config) : config_(std::move(config)), runtime_(config_.runtime), tcp_(config_.tcp), udp_(config_.udp_destination_port, config_.snapshot_interval) {}
ExchangeService::~ExchangeService() { stop(); }
auto ExchangeService::start() -> bool { if (started_.load()) return false; if (!runtime_.start()) return false; if (!tcp_.start([this](const auto& r, auto s){ return onRequest(r,s); }, [this](auto* r, auto* s){ return nextResponse(r,s); })) { runtime_.stop(); return false; } if (!udp_.start([this](auto* u, auto* s){ return nextUpdate(u,s); }, [this](simex::exchange::Snapshot* s){ return snapshotSequence(s); })) { tcp_.stop(); runtime_.stop(); return false; } started_.store(true); return true; }
auto ExchangeService::stop() -> void { started_.store(false); tcp_.stop(); udp_.stop(); runtime_.stop(); }
auto ExchangeService::onRequest(const simex::exchange::ClientRequest& r, std::uint64_t) -> bool { return runtime_.submit(r); }
auto ExchangeService::nextResponse(simex::exchange::ClientResponse* r, std::uint64_t* s) -> bool { if (!r || !s) return false; for (int i=0;i<100;++i) { if (runtime_.responses().tryPop(r)) { *s=response_sequence_++; return true; } std::this_thread::sleep_for(std::chrono::milliseconds(1)); } return false; }
auto ExchangeService::nextUpdate(simex::exchange::MarketUpdate* u, std::uint64_t* s) -> bool { if (!u || !s || !runtime_.updates().tryPop(u)) return false; *s=market_sequence_++; return true; }
auto ExchangeService::snapshotSequence(simex::exchange::Snapshot* snapshot) -> bool { if (!snapshot) return false; *snapshot = runtime_.snapshot(); return true; }
}  // namespace simex::transport
