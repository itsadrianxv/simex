#pragma once

#include "common/reason_code.h"
#include "common/lf_queue.h"
#include "common/types.h"

namespace simex::exchange {

struct ClientRequest final {
  simex::common::RequestType type = simex::common::RequestType::NEW;
  simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
  simex::common::TickerId ticker_id = simex::common::INVALID_TICKER_ID;
  simex::common::ClientOrderId client_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::Side side = simex::common::Side::BUY;
  simex::common::OrderType order_type = simex::common::OrderType::LIMIT;
  simex::common::TimeInForce time_in_force = simex::common::TimeInForce::DAY;
  simex::common::PositionEffect position_effect = simex::common::PositionEffect::OPEN;
  simex::common::PriceTicks price_ticks = simex::common::INVALID_PRICE_TICKS;
  simex::common::Qty qty = 0;
  simex::common::Nanos rx_time = 0;
  auto operator==(const ClientRequest &) const noexcept -> bool = default;
};

struct ClientResponse final {
  simex::common::ResponseType type = simex::common::ResponseType::REJECTED;
  simex::common::ReasonCode reason = simex::common::ReasonCode::NONE;
  simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
  simex::common::TickerId ticker_id = simex::common::INVALID_TICKER_ID;
  simex::common::ClientOrderId client_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::MarketOrderId market_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::Side side = simex::common::Side::BUY;
  simex::common::PositionEffect position_effect = simex::common::PositionEffect::OPEN;
  simex::common::PriceTicks price_ticks = simex::common::INVALID_PRICE_TICKS;
  simex::common::Qty exec_qty = 0;
  simex::common::Qty leaves_qty = 0;
  auto operator==(const ClientResponse &) const noexcept -> bool = default;
};

struct MarketUpdate final {
  simex::common::MarketUpdateType type = simex::common::MarketUpdateType::CANCEL;
  simex::common::TickerId ticker_id = simex::common::INVALID_TICKER_ID;
  simex::common::MarketOrderId market_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::Side side = simex::common::Side::BUY;
  simex::common::PriceTicks price_ticks = simex::common::INVALID_PRICE_TICKS;
  simex::common::Qty qty = 0;
  simex::common::Qty leaves_qty = 0;
  simex::common::Nanos rx_time = 0;
  auto operator==(const MarketUpdate &) const noexcept -> bool = default;
};

using ClientResponseQueue = simex::common::LFQueue<ClientResponse>;
using MarketUpdateQueue = simex::common::LFQueue<MarketUpdate>;

}  // namespace simex::exchange
