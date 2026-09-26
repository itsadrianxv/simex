#pragma once
#include <cstdint>
#include <type_traits>
#include "exchange/messages.h"
namespace simex::transport {
inline constexpr std::uint16_t kProtocolVersion = 1;
enum class MessageKind : std::uint8_t { REQUEST = 1, RESPONSE = 2, SNAPSHOT = 3, UPDATE = 4 };
#pragma pack(push, 1)
struct FrameHeader final { std::uint16_t version = kProtocolVersion; std::uint8_t kind = 0; std::uint8_t reserved = 0; std::uint32_t payload_size = 0; std::uint64_t sequence = 0; };
struct WireRequest final { FrameHeader header{}; simex::exchange::ClientRequest request{}; };
struct WireResponse final { FrameHeader header{}; simex::exchange::ClientResponse response{}; };
struct WireUpdate final { FrameHeader header{}; simex::exchange::MarketUpdate update{}; };
struct WireSnapshotPrefix final { FrameHeader header{}; std::uint32_t order_count = 0; std::uint32_t reserved = 0; };
struct WireSnapshotOrder final { std::uint64_t sequence = 0; simex::exchange::MarketUpdate update{}; };
#pragma pack(pop)
static_assert(std::is_trivially_copyable_v<WireRequest>);
static_assert(std::is_trivially_copyable_v<WireResponse>);
static_assert(std::is_trivially_copyable_v<WireUpdate>);
static_assert(std::is_trivially_copyable_v<WireSnapshotPrefix>);
static_assert(std::is_trivially_copyable_v<WireSnapshotOrder>);
}  // namespace simex::transport
