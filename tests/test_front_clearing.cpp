#include <cassert>

#include "front_clearing/front_clearing.h"

int main() {
  Exchange::FrontClearing clearing;
  const Exchange::ClearingOrder open{1, 10, Common::Side::SELL,
                                    Common::PositionEffect::OPEN, 3};
  assert(clearing.validateAndReserve(open).accepted);
  assert(clearing.onFill(open, 3));
  assert(clearing.position(1).short_today == 3);

  const Exchange::ClearingOrder close{1, 11, Common::Side::BUY,
                                      Common::PositionEffect::CLOSE_TODAY, 2};
  assert(clearing.validateAndReserve(close).accepted);
  assert(clearing.frozenCloseToday(1, Common::Side::BUY) == 2);
  assert(clearing.onFill(close, 1));
  assert(clearing.position(1).short_today == 2);
  assert(clearing.frozenCloseToday(1, Common::Side::BUY) == 1);
  assert(clearing.onCancel(close, 1));
  assert(clearing.frozenCloseToday(1, Common::Side::BUY) == 0);

  const Exchange::ClearingOrder too_large{1, 12, Common::Side::BUY,
                                          Common::PositionEffect::CLOSE_TODAY, 3};
  assert(!clearing.validateAndReserve(too_large).accepted);
  return 0;
}
