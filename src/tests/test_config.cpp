#include <cassert>

#include "common/config.h"

int main(int argc, char **argv) {
  assert(argc == 2);
  const auto config = simex::common::loadSimexConfig(argv[1]);
  assert(config.schema_version == 1);
  assert(config.timezone == "Asia/Shanghai");
  assert(config.instrument.symbol == "RB");
  assert(config.instrument.tick_size == 1);
  assert(config.instrument.max_price_levels == 4096);
  assert(config.instrument.supported_order_combinations.size() == 4);
  assert(config.session_template.night.size() == 3);
  assert(config.session_template.day.size() == 8);
  return 0;
}
