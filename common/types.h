#pragma once

#include <cstdint>
#include <limits>

#include "common/order_types.h"

namespace Common {

using Nanos = std::int64_t;
using TradingDayId = std::uint32_t;
using ClientId = std::uint32_t;
using TickerId = std::uint32_t;
using ClientOrderId = std::uint64_t;
using MarketOrderId = std::uint64_t;
using PriceTicks = std::int64_t;
using Qty = std::uint32_t;

constexpr auto INVALID_CLIENT_ID = std::numeric_limits<ClientId>::max();
constexpr auto INVALID_TICKER_ID = std::numeric_limits<TickerId>::max();
constexpr auto INVALID_ORDER_ID = std::numeric_limits<ClientOrderId>::max();
constexpr auto INVALID_PRICE_TICKS = std::numeric_limits<PriceTicks>::max();

}  // namespace Common
