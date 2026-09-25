#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#include "common/lf_queue.h"
#include "common/types.h"
#include "exchange/messages.h"
#include "front_clearing/front_clearing.h"
#include "matching_engine/me_order_book.h"

namespace Exchange {

class MatchingEngine final {
 public:
  MatchingEngine(Common::InstrumentConfig instrument_config,
                 FrontClearing *front_clearing,
                 ClientResponseQueue *responses,
                 MarketUpdateQueue *market_updates);

  auto setPhase(Common::SessionPhase phase) -> bool;
  [[nodiscard]] auto phase() const noexcept -> Common::SessionPhase { return phase_; }

  /// Seed the previous settlement and derive the current integer-tick price band.
  auto setReferencePrice(Common::PriceTicks previous_settlement_ticks) -> bool;
  auto onTradingDayRollover() -> bool;
  auto cancelAllAtDailyClose() -> bool;

  /// Process one already-sequenced request on the single matching thread.
  auto processClientRequest(const ClientRequest &request) -> bool;
  auto runCallAuction() -> bool;

  [[nodiscard]] auto previousTradePrice() const noexcept -> Common::PriceTicks {
    return previous_trade_price_;
  }
  [[nodiscard]] auto lowerPriceLimit() const noexcept -> Common::PriceTicks {
    return lower_price_limit_;
  }
  [[nodiscard]] auto upperPriceLimit() const noexcept -> Common::PriceTicks {
    return upper_price_limit_;
  }
  [[nodiscard]] auto book() const noexcept -> const MEOrderBook & { return book_; }
  [[nodiscard]] auto canonicalState() const -> std::string;

 private:
  auto processNew(const ClientRequest &request) -> bool;
  auto processCancel(const ClientRequest &request) -> bool;
  auto validateNew(const ClientRequest &request) const -> Common::ReasonCode;
  auto isSupported(const ClientRequest &request) const noexcept -> bool;
  auto emitResponse(ClientResponse response) -> void;
  auto emitMarketUpdate(MarketUpdate update) -> void;
  auto tradePrice(const ClientRequest &aggressor,
                  const RestingOrder &passive) const noexcept -> Common::PriceTicks;
  auto medianPrice(Common::PriceTicks first, Common::PriceTicks second,
                   Common::PriceTicks third) const noexcept -> Common::PriceTicks;
  auto clearingOrder(const ClientRequest &request) const noexcept -> ClearingOrder;
  auto clearingOrder(const RestingOrder &order) const noexcept -> ClearingOrder;

  Common::InstrumentConfig instrument_config_;
  FrontClearing *front_clearing_ = nullptr;
  ClientResponseQueue *responses_ = nullptr;
  MarketUpdateQueue *market_updates_ = nullptr;
  MEOrderBook book_;
  Common::SessionPhase phase_ = Common::SessionPhase::CLOSED;
  Common::PriceTicks previous_settlement_ticks_ = Common::INVALID_PRICE_TICKS;
  Common::PriceTicks previous_trade_price_ = Common::INVALID_PRICE_TICKS;
  Common::PriceTicks lower_price_limit_ = Common::INVALID_PRICE_TICKS;
  Common::PriceTicks upper_price_limit_ = Common::INVALID_PRICE_TICKS;
  Common::MarketOrderId next_market_order_id_ = 1;
  std::uint64_t next_arrival_index_ = 1;
};

}  // namespace Exchange
