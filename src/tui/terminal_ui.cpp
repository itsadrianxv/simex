#include "tui/terminal_ui.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopping = 0;
void onSignal(int) { stopping = 1; }

struct TerminalMode final {
  termios saved{};
  bool active = false;
  ~TerminalMode() {
    if (active) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  }
};

auto stateName(simex::tui::ObserverState state) -> const char * {
  switch (state) {
    case simex::tui::ObserverState::WAITING_SNAPSHOT: return "WAITING SNAPSHOT";
    case simex::tui::ObserverState::SYNCED: return "SYNCED";
    case simex::tui::ObserverState::GAP: return "GAP";
    case simex::tui::ObserverState::DISCONNECTED: return "DISCONNECTED";
  }
  return "UNKNOWN";
}

auto updateName(simex::common::MarketUpdateType type) -> const char * {
  switch (type) {
    case simex::common::MarketUpdateType::ADD: return "ADD";
    case simex::common::MarketUpdateType::MODIFY: return "MODIFY";
    case simex::common::MarketUpdateType::CANCEL: return "CANCEL";
    case simex::common::MarketUpdateType::TRADE: return "TRADE";
  }
  return "UNKNOWN";
}
}

namespace simex::tui {

TerminalUi::TerminalUi(MarketObserver *observer, bool plain) : observer_(observer), plain_(plain) {}

auto TerminalUi::run() -> int {
  std::signal(SIGINT, onSignal);
  TerminalMode mode;
  if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &mode.saved) == 0) {
    auto current = mode.saved;
    current.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    current.c_cc[VMIN] = 0;
    current.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &current) == 0) mode.active = true;
  }
  while (!stopping) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const auto ready = ::poll(&input, 1, 100);
    if (ready > 0 && (input.revents & POLLIN) != 0) {
      char key = 0;
      if (::read(STDIN_FILENO, &key, 1) == 1) {
        if (key == 'q' || key == 'Q') break;
        if (key == 'p' || key == 'P') paused_ = !paused_;
        if (key == '+' && levels_ < 50) ++levels_;
        if (key == '-' && levels_ > 1) --levels_;
      }
    }
    if (!paused_) render(observer_->view());
    if (observer_->view().state == ObserverState::DISCONNECTED) break;
  }
  return 0;
}

auto TerminalUi::render(const ObserverView &view) const -> void {
  if (plain_) renderPlain(view);
  else renderAnsi(view);
}

auto TerminalUi::renderPlain(const ObserverView &view) const -> void {
  std::cout << "state=" << stateName(view.state) << " seq=" << view.sequence
            << " ticker=" << view.ticker_id << " levels=" << levels_;
  if (view.has_trade) std::cout << " last_trade=" << view.last_trade_price << "/" << view.last_trade_qty;
  std::cout << '\n';
  const auto count = std::min(levels_, std::max(view.bids.size(), view.asks.size()));
  for (std::size_t index = 0; index < count; ++index) {
    if (index < view.bids.size()) std::cout << "BID " << view.bids[index].price << ' ' << view.bids[index].order_count << ' ' << view.bids[index].quantity;
    else std::cout << "BID -";
    std::cout << " | ";
    if (index < view.asks.size()) std::cout << "ASK " << view.asks[index].price << ' ' << view.asks[index].order_count << ' ' << view.asks[index].quantity;
    else std::cout << "ASK -";
    std::cout << '\n';
  }
  for (const auto &event : view.events) {
    std::cout << "event=" << updateName(event.update.type) << " seq=" << event.sequence
              << " side=" << simex::common::sideToString(event.update.side)
              << " price=" << event.update.price_ticks << " leaves=" << event.update.leaves_qty
              << " oid=" << event.update.market_order_id << '\n';
  }
  std::cout.flush();
}

auto TerminalUi::renderAnsi(const ObserverView &view) const -> void {
  std::cout << "\033[2J\033[H";
  std::cout << "simex_tui  state=" << stateName(view.state) << " seq=" << view.sequence
            << " ticker=" << view.ticker_id << " levels=" << levels_ << '\n';
  std::cout << "BID PRICE COUNT QTY                 ASK PRICE COUNT QTY\n";
  const auto count = std::min(levels_, std::max(view.bids.size(), view.asks.size()));
  for (std::size_t index = 0; index < count; ++index) {
    if (index < view.bids.size()) std::cout << "" << view.bids[index].price << ' ' << view.bids[index].order_count << ' ' << view.bids[index].quantity;
    else std::cout << "-";
    std::cout << "                         ";
    if (index < view.asks.size()) std::cout << view.asks[index].price << ' ' << view.asks[index].order_count << ' ' << view.asks[index].quantity;
    else std::cout << "-";
    std::cout << '\n';
  }
  if (view.has_trade) std::cout << "LAST TRADE " << view.last_trade_price << " x " << view.last_trade_qty << '\n';
  std::cout << "EVENTS\n";
  for (const auto &event : view.events) {
    std::cout << event.sequence << ' ' << updateName(event.update.type) << ' '
              << simex::common::sideToString(event.update.side) << ' '
              << event.update.price_ticks << " leaves=" << event.update.leaves_qty
              << " oid=" << event.update.market_order_id << '\n';
  }
  std::cout << "\n[p]ause  [+/-] levels  [q]uit\n" << std::flush;
}

}  // namespace simex::tui
