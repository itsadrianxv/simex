#pragma once

#include <cstdint>
#include <string_view>

namespace simex::common {

enum class Side : std::int8_t {
  BUY = 1,
  SELL = -1,
};

enum class OrderType : std::uint8_t {
  LIMIT,
  MARKET,
};

enum class TimeInForce : std::uint8_t {
  DAY,
  IOC,
  FOK,
};

enum class PositionEffect : std::uint8_t {
  OPEN,
  CLOSE_TODAY,
  CLOSE_YESTERDAY,
};

enum class RequestType : std::uint8_t {
  NEW,
  CANCEL,
};

enum class ResponseType : std::uint8_t {
  ACCEPTED,
  REJECTED,
  CANCELED,
  FILLED,
  CANCEL_REJECTED,
};

enum class MarketUpdateType : std::uint8_t {
  ADD,
  MODIFY,
  CANCEL,
  TRADE,
};

enum class SessionPhase : std::uint8_t {
  CLOSED,
  AUCTION_SUBMIT,
  AUCTION_MATCH,
  CONTINUOUS,
  BREAK,
};

constexpr auto sideToString(Side side) noexcept -> std::string_view {
  return side == Side::BUY ? "BUY" : "SELL";
}

constexpr auto orderTypeToString(OrderType type) noexcept -> std::string_view {
  return type == OrderType::LIMIT ? "LIMIT" : "MARKET";
}

constexpr auto timeInForceToString(TimeInForce tif) noexcept -> std::string_view {
  switch (tif) {
    case TimeInForce::DAY:
      return "DAY";
    case TimeInForce::IOC:
      return "IOC";
    case TimeInForce::FOK:
      return "FOK";
  }
  return "UNKNOWN";
}

constexpr auto positionEffectToString(PositionEffect effect) noexcept -> std::string_view {
  switch (effect) {
    case PositionEffect::OPEN:
      return "OPEN";
    case PositionEffect::CLOSE_TODAY:
      return "CLOSE_TODAY";
    case PositionEffect::CLOSE_YESTERDAY:
      return "CLOSE_YESTERDAY";
  }
  return "UNKNOWN";
}

constexpr auto sessionPhaseToString(SessionPhase phase) noexcept -> std::string_view {
  switch (phase) {
    case SessionPhase::CLOSED:
      return "CLOSED";
    case SessionPhase::AUCTION_SUBMIT:
      return "AUCTION_SUBMIT";
    case SessionPhase::AUCTION_MATCH:
      return "AUCTION_MATCH";
    case SessionPhase::CONTINUOUS:
      return "CONTINUOUS";
    case SessionPhase::BREAK:
      return "BREAK";
  }
  return "UNKNOWN";
}

}  // namespace simex::common
