#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#include "common/lf_queue.h"
#include "common/types.h"
#include "exchange/messages.h"
#include "front_clearing/front_clearing.h"
#include "matching_engine/me_order_book.h"

namespace simex::exchange {

class MatchingEngine final {
 public:
  MatchingEngine(simex::common::InstrumentConfig instrument_config,
                 FrontClearing *front_clearing,
                 ClientResponseQueue *responses,
                 MarketUpdateQueue *market_updates);

  auto setPhase(simex::common::SessionPhase phase) -> bool;
  [[nodiscard]] auto phase() const noexcept -> simex::common::SessionPhase { return phase_; }

  /// Seed the previous settlement and derive the current integer-tick price band.
  auto setReferencePrice(simex::common::PriceTicks previous_settlement_ticks) -> bool;
  auto onTradingDayRollover() -> bool;
  auto cancelAllAtDailyClose() -> bool;

  /// Process one already-sequenced request on the single matching thread.
  auto processClientRequest(const ClientRequest &request) -> bool;
  auto runCallAuction() -> bool;

  [[nodiscard]] auto previousTradePrice() const noexcept -> simex::common::PriceTicks {
    return previous_trade_price_;
  }
  [[nodiscard]] auto lowerPriceLimit() const noexcept -> simex::common::PriceTicks {
    return lower_price_limit_;
  }
  [[nodiscard]] auto upperPriceLimit() const noexcept -> simex::common::PriceTicks {
    return upper_price_limit_;
  }
  [[nodiscard]] auto book() const noexcept -> const MEOrderBook & { return book_; }
  [[nodiscard]] auto canonicalState() const -> std::string;

 private:
  auto processNew(const ClientRequest &request) -> bool;
  auto processCancel(const ClientRequest &request) -> bool;
  auto validateNew(const ClientRequest &request) const -> simex::common::ReasonCode;
  auto isSupported(const ClientRequest &request) const noexcept -> bool;
  auto emitResponse(ClientResponse response) -> void;
  auto emitMarketUpdate(MarketUpdate update) -> void;
  auto tradePrice(const ClientRequest &aggressor,
                  const RestingOrder &passive) const noexcept -> simex::common::PriceTicks;
  auto medianPrice(simex::common::PriceTicks first, simex::common::PriceTicks second,
                   simex::common::PriceTicks third) const noexcept -> simex::common::PriceTicks;
  auto clearingOrder(const ClientRequest &request) const noexcept -> ClearingOrder;
  auto clearingOrder(const RestingOrder &order) const noexcept -> ClearingOrder;

  simex::common::InstrumentConfig instrument_config_;
  FrontClearing *front_clearing_ = nullptr;
  ClientResponseQueue *responses_ = nullptr;
  MarketUpdateQueue *market_updates_ = nullptr;
  MEOrderBook book_;
  simex::common::SessionPhase phase_ = simex::common::SessionPhase::CLOSED;
  simex::common::PriceTicks previous_settlement_ticks_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks previous_trade_price_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks lower_price_limit_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks upper_price_limit_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::MarketOrderId next_market_order_id_ = 1;
  std::uint64_t next_arrival_index_ = 1;
};

}  // namespace simex::exchange
