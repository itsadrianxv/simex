#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/reason_code.h"
#include "common/types.h"

namespace simex::exchange {

struct PositionState final {
  simex::common::Qty long_today = 0;
  simex::common::Qty long_yesterday = 0;
  simex::common::Qty short_today = 0;
  simex::common::Qty short_yesterday = 0;
};

struct ClearingOrder final {
  simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
  simex::common::ClientOrderId client_order_id = simex::common::INVALID_ORDER_ID;
  simex::common::Side side = simex::common::Side::BUY;
  simex::common::PositionEffect position_effect = simex::common::PositionEffect::OPEN;
  simex::common::Qty qty = 0;
};

struct ClearingAdmission final {
  bool accepted = false;
  simex::common::ReasonCode reason = simex::common::ReasonCode::NONE;
};

/// Thin today/yesterday position reservation owned by the matching thread.
class FrontClearing final {
 public:
  FrontClearing() = default;

  auto validateAndReserve(const ClearingOrder &order) -> ClearingAdmission;
  auto onFill(const ClearingOrder &order, simex::common::Qty fill_qty) -> bool;
  auto onCancel(const ClearingOrder &order, simex::common::Qty leaves_qty) -> bool;
  auto onTradingDayRollover() -> bool;
  auto seedPosition(simex::common::ClientId client_id, const PositionState &position) -> bool;
  [[nodiscard]] auto canonicalState() const -> std::string;

  [[nodiscard]] auto position(simex::common::ClientId client_id) const noexcept
      -> PositionState;
  [[nodiscard]] auto frozenCloseToday(simex::common::ClientId client_id,
                                      simex::common::Side side) const noexcept -> simex::common::Qty;
  [[nodiscard]] auto frozenCloseYesterday(simex::common::ClientId client_id,
                                           simex::common::Side side) const noexcept -> simex::common::Qty;

 private:
  struct AccountState final {
    PositionState position;
    simex::common::Qty frozen_long_today = 0;
    simex::common::Qty frozen_long_yesterday = 0;
    simex::common::Qty frozen_short_today = 0;
    simex::common::Qty frozen_short_yesterday = 0;
  };

  struct OrderKey final {
    simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
    simex::common::ClientOrderId client_order_id = simex::common::INVALID_ORDER_ID;

    auto operator==(const OrderKey &) const noexcept -> bool = default;
  };

  struct OrderKeyHash final {
    auto operator()(const OrderKey &key) const noexcept -> std::size_t {
      return (static_cast<std::size_t>(key.client_id) << 32U) ^
             static_cast<std::size_t>(key.client_order_id);
    }
  };

  struct Reservation final {
    ClearingOrder order;
    simex::common::Qty remaining = 0;
  };

  auto account(simex::common::ClientId client_id) -> AccountState &;
  auto available(const AccountState &account_state, const ClearingOrder &order) const noexcept
      -> simex::common::Qty;
  simex::common::Qty &frozenSlot(AccountState &account_state,
                          const ClearingOrder &order) noexcept;
  auto frozenSlot(const AccountState &account_state, const ClearingOrder &order) const noexcept
      -> simex::common::Qty;
  simex::common::Qty &positionSlot(PositionState &position_state,
                            const ClearingOrder &order) noexcept;
  auto positionSlot(const PositionState &position_state, const ClearingOrder &order) const noexcept
      -> simex::common::Qty;

  std::unordered_map<simex::common::ClientId, AccountState> accounts_;
  std::unordered_map<OrderKey, Reservation, OrderKeyHash> reservations_;
};

}  // namespace simex::exchange
