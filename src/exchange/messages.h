#pragma once

#include "common/reason_code.h"
#include "common/lf_queue.h"
#include "common/types.h"

namespace Exchange {

struct ClientRequest final {
  Common::RequestType type = Common::RequestType::NEW;
  Common::ClientId client_id = Common::INVALID_CLIENT_ID;
  Common::TickerId ticker_id = Common::INVALID_TICKER_ID;
  Common::ClientOrderId client_order_id = Common::INVALID_ORDER_ID;
  Common::Side side = Common::Side::BUY;
  Common::OrderType order_type = Common::OrderType::LIMIT;
  Common::TimeInForce time_in_force = Common::TimeInForce::DAY;
  Common::PositionEffect position_effect = Common::PositionEffect::OPEN;
  Common::PriceTicks price_ticks = Common::INVALID_PRICE_TICKS;
  Common::Qty qty = 0;
  Common::Nanos rx_time = 0;
  auto operator==(const ClientRequest &) const noexcept -> bool = default;
};

struct ClientResponse final {
  Common::ResponseType type = Common::ResponseType::REJECTED;
  Common::ReasonCode reason = Common::ReasonCode::NONE;
  Common::ClientId client_id = Common::INVALID_CLIENT_ID;
  Common::TickerId ticker_id = Common::INVALID_TICKER_ID;
  Common::ClientOrderId client_order_id = Common::INVALID_ORDER_ID;
  Common::MarketOrderId market_order_id = Common::INVALID_ORDER_ID;
  Common::Side side = Common::Side::BUY;
  Common::PositionEffect position_effect = Common::PositionEffect::OPEN;
  Common::PriceTicks price_ticks = Common::INVALID_PRICE_TICKS;
  Common::Qty exec_qty = 0;
  Common::Qty leaves_qty = 0;
  auto operator==(const ClientResponse &) const noexcept -> bool = default;
};

struct MarketUpdate final {
  Common::MarketUpdateType type = Common::MarketUpdateType::CANCEL;
  Common::TickerId ticker_id = Common::INVALID_TICKER_ID;
  Common::MarketOrderId market_order_id = Common::INVALID_ORDER_ID;
  Common::Side side = Common::Side::BUY;
  Common::PriceTicks price_ticks = Common::INVALID_PRICE_TICKS;
  Common::Qty qty = 0;
  Common::Qty leaves_qty = 0;
  Common::Nanos rx_time = 0;
  auto operator==(const MarketUpdate &) const noexcept -> bool = default;
};

using ClientResponseQueue = Common::LFQueue<ClientResponse>;
using MarketUpdateQueue = Common::LFQueue<MarketUpdate>;

}  // namespace Exchange
