#include <cassert>

#include "front_clearing/front_clearing.h"

int main() {
  simex::exchange::FrontClearing clearing;
  const simex::exchange::ClearingOrder open{1, 10, simex::common::Side::SELL,
                                    simex::common::PositionEffect::OPEN, 3};
  assert(clearing.validateAndReserve(open).accepted);
  assert(clearing.onFill(open, 3));
  assert(clearing.position(1).short_today == 3);

  const simex::exchange::ClearingOrder close{1, 11, simex::common::Side::BUY,
                                      simex::common::PositionEffect::CLOSE_TODAY, 2};
  assert(clearing.validateAndReserve(close).accepted);
  assert(clearing.frozenCloseToday(1, simex::common::Side::BUY) == 2);
  assert(clearing.onFill(close, 1));
  assert(clearing.position(1).short_today == 2);
  assert(clearing.frozenCloseToday(1, simex::common::Side::BUY) == 1);
  assert(clearing.onCancel(close, 1));
  assert(clearing.frozenCloseToday(1, simex::common::Side::BUY) == 0);

  const simex::exchange::ClearingOrder too_large{1, 12, simex::common::Side::BUY,
                                          simex::common::PositionEffect::CLOSE_TODAY, 3};
  assert(!clearing.validateAndReserve(too_large).accepted);
  return 0;
}
