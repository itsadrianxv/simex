#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "tui/market_observer.h"
#include "tui/terminal_ui.h"

auto main(int argc, char **argv) -> int {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: simex_tui <udp-port> [--plain]\n";
    return 2;
  }
  std::uint16_t port = 0;
  const auto text = std::string_view(argv[1]);
  const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || port == 0) {
    std::cerr << "invalid UDP port\n";
    return 2;
  }
  const bool plain = argc == 3 && std::string_view(argv[2]) == "--plain";
  if (argc == 3 && !plain) {
    std::cerr << "usage: simex_tui <udp-port> [--plain]\n";
    return 2;
  }
  simex::tui::MarketObserver observer(port);
  if (!observer.start()) {
    std::cerr << "cannot bind UDP loopback port " << port << '\n';
    return 1;
  }
  simex::tui::TerminalUi ui(&observer, plain);
  const auto result_code = ui.run();
  observer.stop();
  return result_code;
}
