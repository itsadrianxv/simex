#pragma once

#include <cstdint>
#include <string_view>

namespace simex::common {

enum class ReasonCode : std::uint16_t {
  NONE,
  INVALID_TICK,
  INVALID_PRICE,
  INVALID_QTY,
  PRICE_LIMIT_EXCEEDED,
  REFERENCE_PRICE_UNAVAILABLE,
  SESSION_CLOSED,
  ORDER_TYPE_NOT_ALLOWED,
  INSUFFICIENT_CLOSE_TODAY,
  INSUFFICIENT_CLOSE_YESTERDAY,
  FOK_NOT_FILLED,
  DUPLICATE_ORDER_ID,
  SESSION_END,
  ORDER_NOT_FOUND,
  QUEUE_FULL,
};

constexpr auto reasonCodeToString(ReasonCode code) noexcept -> std::string_view {
  switch (code) {
    case ReasonCode::NONE:
      return "NONE";
    case ReasonCode::INVALID_TICK:
      return "INVALID_TICK";
    case ReasonCode::INVALID_PRICE:
      return "INVALID_PRICE";
    case ReasonCode::INVALID_QTY:
      return "INVALID_QTY";
    case ReasonCode::PRICE_LIMIT_EXCEEDED:
      return "PRICE_LIMIT_EXCEEDED";
    case ReasonCode::REFERENCE_PRICE_UNAVAILABLE:
      return "REFERENCE_PRICE_UNAVAILABLE";
    case ReasonCode::SESSION_CLOSED:
      return "SESSION_CLOSED";
    case ReasonCode::ORDER_TYPE_NOT_ALLOWED:
      return "ORDER_TYPE_NOT_ALLOWED";
    case ReasonCode::INSUFFICIENT_CLOSE_TODAY:
      return "INSUFFICIENT_CLOSE_TODAY";
    case ReasonCode::INSUFFICIENT_CLOSE_YESTERDAY:
      return "INSUFFICIENT_CLOSE_YESTERDAY";
    case ReasonCode::FOK_NOT_FILLED:
      return "FOK_NOT_FILLED";
    case ReasonCode::DUPLICATE_ORDER_ID:
      return "DUPLICATE_ORDER_ID";
    case ReasonCode::SESSION_END:
      return "SESSION_END";
    case ReasonCode::ORDER_NOT_FOUND:
      return "ORDER_NOT_FOUND";
    case ReasonCode::QUEUE_FULL:
      return "QUEUE_FULL";
  }
  return "UNKNOWN";
}

}  // namespace simex::common
