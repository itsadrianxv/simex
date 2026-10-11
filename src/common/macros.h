#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

/// Branch prediction hints.
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/// Check condition and exit if not true. The message expression is only
/// evaluated (and any std::string built) on the failure path, so the hot
/// path pays nothing when the assertion holds.
#define SIMEX_ASSERT(cond, msg)                                               \
  do {                                                                        \
    if (UNLIKELY(!(cond))) {                                                  \
      simex::common::assertFail(msg);                                         \
    }                                                                         \
  } while (false)

namespace simex::common {

/// Print the assertion message and terminate, mirroring the reference
/// implementation's ASSERT/FATAL behavior.
inline auto assertFail(const std::string &msg) noexcept -> void {
  std::cerr << "ASSERT : " << msg << std::endl;

  std::exit(EXIT_FAILURE);
}

}  // namespace simex::common
