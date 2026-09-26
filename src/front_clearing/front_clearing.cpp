#include "front_clearing/front_clearing.h"

#include <limits>
#include <algorithm>
#include <sstream>

namespace simex::exchange {

auto FrontClearing::account(simex::common::ClientId client_id) -> AccountState & {
  return accounts_[client_id];
}

simex::common::Qty &FrontClearing::positionSlot(PositionState &position_state,
                                         const ClearingOrder &order) noexcept {
  if (order.position_effect == simex::common::PositionEffect::CLOSE_TODAY) {
    return order.side == simex::common::Side::BUY ? position_state.short_today
                                           : position_state.long_today;
  }
  return order.side == simex::common::Side::BUY ? position_state.short_yesterday
                                         : position_state.long_yesterday;
}

auto FrontClearing::positionSlot(const PositionState &position_state,
                                 const ClearingOrder &order) const noexcept -> simex::common::Qty {
  if (order.position_effect == simex::common::PositionEffect::CLOSE_TODAY) {
    return order.side == simex::common::Side::BUY ? position_state.short_today
                                           : position_state.long_today;
  }
  return order.side == simex::common::Side::BUY ? position_state.short_yesterday
                                         : position_state.long_yesterday;
}

simex::common::Qty &FrontClearing::frozenSlot(AccountState &account_state,
                                      const ClearingOrder &order) noexcept {
  if (order.position_effect == simex::common::PositionEffect::CLOSE_TODAY) {
    return order.side == simex::common::Side::BUY ? account_state.frozen_short_today
                                           : account_state.frozen_long_today;
  }
  return order.side == simex::common::Side::BUY ? account_state.frozen_short_yesterday
                                         : account_state.frozen_long_yesterday;
}

auto FrontClearing::frozenSlot(const AccountState &account_state,
                               const ClearingOrder &order) const noexcept -> simex::common::Qty {
  if (order.position_effect == simex::common::PositionEffect::CLOSE_TODAY) {
    return order.side == simex::common::Side::BUY ? account_state.frozen_short_today
                                           : account_state.frozen_long_today;
  }
  return order.side == simex::common::Side::BUY ? account_state.frozen_short_yesterday
                                         : account_state.frozen_long_yesterday;
}

auto FrontClearing::available(const AccountState &account_state,
                              const ClearingOrder &order) const noexcept -> simex::common::Qty {
  if (order.position_effect == simex::common::PositionEffect::OPEN) {
    return std::numeric_limits<simex::common::Qty>::max();
  }
  const auto held = positionSlot(account_state.position, order);
  const auto frozen = frozenSlot(account_state, order);
  return held >= frozen ? held - frozen : 0;
}

auto FrontClearing::validateAndReserve(const ClearingOrder &order) -> ClearingAdmission {
  if (order.client_id == simex::common::INVALID_CLIENT_ID ||
      order.client_order_id == simex::common::INVALID_ORDER_ID || order.qty == 0) {
    return {false, simex::common::ReasonCode::INVALID_QTY};
  }

  const OrderKey key{order.client_id, order.client_order_id};
  if (reservations_.contains(key)) {
    return {false, simex::common::ReasonCode::DUPLICATE_ORDER_ID};
  }

  AccountState *account_state = nullptr;
  if (order.position_effect == simex::common::PositionEffect::OPEN) {
    account_state = &account(order.client_id);
  } else {
    const auto found_account = accounts_.find(order.client_id);
    if (found_account == accounts_.end() || available(found_account->second, order) < order.qty) {
      return {false, order.position_effect == simex::common::PositionEffect::CLOSE_TODAY
                         ? simex::common::ReasonCode::INSUFFICIENT_CLOSE_TODAY
                         : simex::common::ReasonCode::INSUFFICIENT_CLOSE_YESTERDAY};
    }
    account_state = &found_account->second;
  }

  if (order.position_effect != simex::common::PositionEffect::OPEN &&
      available(*account_state, order) < order.qty) {
    return {false, order.position_effect == simex::common::PositionEffect::CLOSE_TODAY
                       ? simex::common::ReasonCode::INSUFFICIENT_CLOSE_TODAY
                       : simex::common::ReasonCode::INSUFFICIENT_CLOSE_YESTERDAY};
  }

  if (order.position_effect != simex::common::PositionEffect::OPEN) {
    auto &frozen = frozenSlot(*account_state, order);
    frozen += order.qty;
  }
  reservations_.emplace(key, Reservation{order, order.qty});
  return {true, simex::common::ReasonCode::NONE};
}

auto FrontClearing::onFill(const ClearingOrder &order, simex::common::Qty fill_qty) -> bool {
  if (fill_qty == 0 || fill_qty > order.qty) {
    return false;
  }

  const OrderKey key{order.client_id, order.client_order_id};
  const auto found = reservations_.find(key);
  if (found == reservations_.end() || found->second.remaining < fill_qty) {
    return false;
  }

  auto &account_state = account(order.client_id);
  if (order.position_effect == simex::common::PositionEffect::OPEN) {
    auto &position = order.side == simex::common::Side::BUY ? account_state.position.long_today
                                                     : account_state.position.short_today;
    position += fill_qty;
  } else {
    auto &position = positionSlot(account_state.position, order);
    if (position < fill_qty) {
      return false;
    }
    position -= fill_qty;
    frozenSlot(account_state, order) -= fill_qty;
  }

  found->second.remaining -= fill_qty;
  if (found->second.remaining == 0) {
    reservations_.erase(found);
  }
  return true;
}

auto FrontClearing::onCancel(const ClearingOrder &order, simex::common::Qty leaves_qty) -> bool {
  const OrderKey key{order.client_id, order.client_order_id};
  const auto found = reservations_.find(key);
  if (found == reservations_.end()) {
    return order.position_effect == simex::common::PositionEffect::OPEN && leaves_qty == 0;
  }
  if (leaves_qty != found->second.remaining) {
    return false;
  }

  auto &account_state = account(order.client_id);
  if (order.position_effect != simex::common::PositionEffect::OPEN) {
    frozenSlot(account_state, order) -= leaves_qty;
  }
  reservations_.erase(found);
  return true;
}

auto FrontClearing::onTradingDayRollover() -> bool {
  if (!reservations_.empty()) {
    return false;
  }
  for (auto &[client_id, account_state] : accounts_) {
    account_state.position.long_yesterday = account_state.position.long_today;
    account_state.position.short_yesterday = account_state.position.short_today;
    account_state.position.long_today = 0;
    account_state.position.short_today = 0;
    account_state.frozen_long_today = 0;
    account_state.frozen_short_today = 0;
    account_state.frozen_long_yesterday = 0;
    account_state.frozen_short_yesterday = 0;
  }
  return true;
}

auto FrontClearing::seedPosition(simex::common::ClientId client_id,
                                 const PositionState &position) -> bool {
  if (client_id == simex::common::INVALID_CLIENT_ID) return false;
  for (const auto &[key, reservation] : reservations_) {
    if (key.client_id == client_id) return false;
  }
  auto &account_state = account(client_id);
  if (account_state.frozen_long_today != 0 || account_state.frozen_long_yesterday != 0 ||
      account_state.frozen_short_today != 0 || account_state.frozen_short_yesterday != 0) {
    return false;
  }
  account_state.position = position;
  return true;
}

auto FrontClearing::canonicalState() const -> std::string {
  std::vector<simex::common::ClientId> client_ids;
  client_ids.reserve(accounts_.size());
  for (const auto &[client_id, account_state] : accounts_) {
    (void)account_state;
    client_ids.push_back(client_id);
  }
  std::sort(client_ids.begin(), client_ids.end());

  std::ostringstream output;
  for (const auto client_id : client_ids) {
    const auto &account_state = accounts_.at(client_id);
    output << client_id << ':' << account_state.position.long_today << ':'
           << account_state.position.long_yesterday << ':'
           << account_state.position.short_today << ':'
           << account_state.position.short_yesterday << ':'
           << account_state.frozen_long_today << ':'
           << account_state.frozen_long_yesterday << ':'
           << account_state.frozen_short_today << ':'
           << account_state.frozen_short_yesterday << ';';
  }
  std::vector<OrderKey> reservation_keys;
  reservation_keys.reserve(reservations_.size());
  for (const auto &[key, reservation] : reservations_) {
    (void)reservation;
    reservation_keys.push_back(key);
  }
  std::sort(reservation_keys.begin(), reservation_keys.end(), [](const auto &left, const auto &right) {
    if (left.client_id != right.client_id) return left.client_id < right.client_id;
    return left.client_order_id < right.client_order_id;
  });
  for (const auto &key : reservation_keys) {
    const auto &reservation = reservations_.at(key);
    output << 'r' << key.client_id << ':' << key.client_order_id << ':'
           << reservation.remaining << ';';
  }
  return output.str();
}

auto FrontClearing::position(simex::common::ClientId client_id) const noexcept -> PositionState {
  const auto found = accounts_.find(client_id);
  return found == accounts_.end() ? PositionState{} : found->second.position;
}

auto FrontClearing::frozenCloseToday(simex::common::ClientId client_id,
                                     simex::common::Side side) const noexcept -> simex::common::Qty {
  const auto found = accounts_.find(client_id);
  if (found == accounts_.end()) return 0;
  return side == simex::common::Side::BUY ? found->second.frozen_short_today
                                   : found->second.frozen_long_today;
}

auto FrontClearing::frozenCloseYesterday(simex::common::ClientId client_id,
                                         simex::common::Side side) const noexcept -> simex::common::Qty {
  const auto found = accounts_.find(client_id);
  if (found == accounts_.end()) return 0;
  return side == simex::common::Side::BUY ? found->second.frozen_short_yesterday
                                   : found->second.frozen_long_yesterday;
}

}  // namespace simex::exchange
