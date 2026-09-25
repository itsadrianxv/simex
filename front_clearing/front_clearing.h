#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/reason_code.h"
#include "common/types.h"

namespace Exchange {

struct PositionState final {
  Common::Qty long_today = 0;
  Common::Qty long_yesterday = 0;
  Common::Qty short_today = 0;
  Common::Qty short_yesterday = 0;
};

struct ClearingOrder final {
  Common::ClientId client_id = Common::INVALID_CLIENT_ID;
  Common::ClientOrderId client_order_id = Common::INVALID_ORDER_ID;
  Common::Side side = Common::Side::BUY;
  Common::PositionEffect position_effect = Common::PositionEffect::OPEN;
  Common::Qty qty = 0;
};

struct ClearingAdmission final {
  bool accepted = false;
  Common::ReasonCode reason = Common::ReasonCode::NONE;
};

/// Thin today/yesterday position reservation owned by the matching thread.
class FrontClearing final {
 public:
  FrontClearing() = default;

  auto validateAndReserve(const ClearingOrder &order) -> ClearingAdmission;
  auto onFill(const ClearingOrder &order, Common::Qty fill_qty) -> bool;
  auto onCancel(const ClearingOrder &order, Common::Qty leaves_qty) -> bool;
  auto onTradingDayRollover() -> bool;
  auto seedPosition(Common::ClientId client_id, const PositionState &position) -> bool;
  [[nodiscard]] auto canonicalState() const -> std::string;

  [[nodiscard]] auto position(Common::ClientId client_id) const noexcept
      -> PositionState;
  [[nodiscard]] auto frozenCloseToday(Common::ClientId client_id,
                                      Common::Side side) const noexcept -> Common::Qty;
  [[nodiscard]] auto frozenCloseYesterday(Common::ClientId client_id,
                                           Common::Side side) const noexcept -> Common::Qty;

 private:
  struct AccountState final {
    PositionState position;
    Common::Qty frozen_long_today = 0;
    Common::Qty frozen_long_yesterday = 0;
    Common::Qty frozen_short_today = 0;
    Common::Qty frozen_short_yesterday = 0;
  };

  struct OrderKey final {
    Common::ClientId client_id = Common::INVALID_CLIENT_ID;
    Common::ClientOrderId client_order_id = Common::INVALID_ORDER_ID;

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
    Common::Qty remaining = 0;
  };

  auto account(Common::ClientId client_id) -> AccountState &;
  auto available(const AccountState &account_state, const ClearingOrder &order) const noexcept
      -> Common::Qty;
  Common::Qty &frozenSlot(AccountState &account_state,
                          const ClearingOrder &order) noexcept;
  auto frozenSlot(const AccountState &account_state, const ClearingOrder &order) const noexcept
      -> Common::Qty;
  Common::Qty &positionSlot(PositionState &position_state,
                            const ClearingOrder &order) noexcept;
  auto positionSlot(const PositionState &position_state, const ClearingOrder &order) const noexcept
      -> Common::Qty;

  std::unordered_map<Common::ClientId, AccountState> accounts_;
  std::unordered_map<OrderKey, Reservation, OrderKeyHash> reservations_;
};

}  // namespace Exchange
