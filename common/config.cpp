#include "common/config.h"

#include <cmath>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace Common {
namespace {

using Json = nlohmann::json;

auto requireField(const Json &object, const char *name) -> const Json & {
  if (!object.contains(name)) {
    throw std::runtime_error(std::string("missing config field: ") + name);
  }
  return object.at(name);
}

auto parseTime(const std::string &value) -> int {
  if (value.size() != 5 || value[2] != ':' ||
      !std::isdigit(static_cast<unsigned char>(value[0])) ||
      !std::isdigit(static_cast<unsigned char>(value[1])) ||
      !std::isdigit(static_cast<unsigned char>(value[3])) ||
      !std::isdigit(static_cast<unsigned char>(value[4]))) {
    throw std::runtime_error("invalid local time, expected HH:MM: " + value);
  }

  const auto hour = std::stoi(value.substr(0, 2));
  const auto minute = std::stoi(value.substr(3, 2));
  if (hour > 23 || minute > 59) {
    throw std::runtime_error("invalid local time, expected HH:MM: " + value);
  }
  return hour * 60 + minute;
}

auto parsePhase(const std::string &value) -> SessionPhase {
  if (value == "CLOSED") return SessionPhase::CLOSED;
  if (value == "AUCTION_SUBMIT") return SessionPhase::AUCTION_SUBMIT;
  if (value == "AUCTION_MATCH") return SessionPhase::AUCTION_MATCH;
  if (value == "CONTINUOUS") return SessionPhase::CONTINUOUS;
  if (value == "BREAK") return SessionPhase::BREAK;
  throw std::runtime_error("unknown session phase: " + value);
}

auto parseWindow(const Json &json) -> SessionWindowConfig {
  SessionWindowConfig window;
  window.phase = parsePhase(requireField(json, "phase").get<std::string>());
  window.start_minute = parseTime(requireField(json, "start").get<std::string>());
  window.end_minute = parseTime(requireField(json, "end").get<std::string>());
  window.crosses_midnight = json.value("crosses_midnight", false);
  if (!window.crosses_midnight && window.end_minute <= window.start_minute) {
    throw std::runtime_error("session window end must be after start");
  }
  if (window.crosses_midnight && window.end_minute >= window.start_minute) {
    throw std::runtime_error("cross-midnight session window must wrap time");
  }
  return window;
}

auto parseOrderType(const std::string &value) -> OrderType {
  if (value == "LIMIT") return OrderType::LIMIT;
  if (value == "MARKET") return OrderType::MARKET;
  throw std::runtime_error("unknown order type: " + value);
}

auto parseTimeInForce(const std::string &value) -> TimeInForce {
  if (value == "DAY") return TimeInForce::DAY;
  if (value == "IOC") return TimeInForce::IOC;
  if (value == "FOK") return TimeInForce::FOK;
  throw std::runtime_error("unknown time in force: " + value);
}

template <typename T, typename Parser>
auto parseEnumArray(const Json &json, const char *field, Parser parser) -> std::vector<T> {
  std::vector<T> result;
  for (const auto &value : requireField(json, field)) {
    result.push_back(parser(value.get<std::string>()));
  }
  return result;
}

}  // namespace

auto loadSimexConfig(const std::filesystem::path &path) -> SimexConfig {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open config: " + path.string());
  }

  Json json;
  input >> json;

  SimexConfig config;
  config.schema_version = requireField(json, "schema_version").get<int>();
  if (config.schema_version != 1) {
    throw std::runtime_error("unsupported config schema_version");
  }

  const auto &venue = requireField(json, "venue");
  config.timezone = requireField(venue, "timezone").get<std::string>();
  if (config.timezone != "Asia/Shanghai") {
    throw std::runtime_error("only Asia/Shanghai is supported in the first profile");
  }

  const auto &instrument = requireField(json, "instrument");
  config.instrument.symbol = requireField(instrument, "symbol").get<std::string>();
  config.instrument.profile = requireField(instrument, "profile").get<std::string>();
  config.instrument.exchange = requireField(instrument, "exchange").get<std::string>();
  config.instrument.contract_size_tons = requireField(instrument, "contract_size_tons").get<Qty>();
  config.instrument.tick_size = requireField(instrument, "tick_size").get<PriceTicks>();
  config.instrument.max_price_levels = requireField(instrument, "max_price_levels").get<std::size_t>();
  config.instrument.price_limit_percent = requireField(requireField(instrument, "price_limit"), "absolute_percent").get<double>();
  if (config.instrument.tick_size <= 0 || config.instrument.max_price_levels == 0 ||
      config.instrument.contract_size_tons == 0 ||
      !std::isfinite(config.instrument.price_limit_percent) ||
      config.instrument.price_limit_percent <= 0.0) {
    throw std::runtime_error("invalid instrument limits");
  }

  const auto &orders = requireField(json, "orders");
  for (const auto &combination : requireField(orders, "supported_combinations")) {
    config.instrument.supported_order_combinations.push_back({
        parseOrderType(requireField(combination, "order_type").get<std::string>()),
        parseTimeInForce(requireField(combination, "time_in_force").get<std::string>())});
  }
  config.instrument.auction_allowed_order_types =
      parseEnumArray<OrderType>(orders, "auction_allowed_order_types", parseOrderType);
  config.instrument.auction_allowed_time_in_force =
      parseEnumArray<TimeInForce>(orders, "auction_allowed_time_in_force", parseTimeInForce);

  const auto &sessions = requireField(json, "session_template");
  for (const auto &window : requireField(sessions, "night")) {
    config.session_template.night.push_back(parseWindow(window));
  }
  for (const auto &window : requireField(sessions, "day")) {
    config.session_template.day.push_back(parseWindow(window));
  }
  config.session_template.overnight_closed = parseWindow(requireField(sessions, "overnight_closed"));
  config.session_template.daily_close_minute =
      parseTime(requireField(requireField(sessions, "daily_close"), "at").get<std::string>());

  if (config.session_template.night.empty() || config.session_template.day.empty()) {
    throw std::runtime_error("session template must contain night and day windows");
  }
  return config;
}

}  // namespace Common
