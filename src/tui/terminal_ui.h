#pragma once

#include <cstddef>

#include "tui/market_observer.h"

namespace simex::tui {

class TerminalUi final {
 public:
  TerminalUi(MarketObserver *observer, bool plain);
  auto run() -> int;

 private:
  auto render(const ObserverView &view) const -> void;
  auto renderPlain(const ObserverView &view) const -> void;
  auto renderAnsi(const ObserverView &view) const -> void;

  MarketObserver *observer_ = nullptr;
  bool plain_ = false;
  std::size_t levels_ = 10;
  bool paused_ = false;
};

}  // namespace simex::tui
