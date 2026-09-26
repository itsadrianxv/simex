#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <thread>
#include <nlohmann/json.hpp>

#include "common/config.h"
#include "participant_simulator/participant_simulator.h"
#include "session_calendar/session_calendar.h"
#include "transport/exchange_service.h"

namespace {
volatile std::sig_atomic_t stopping = 0;
void onSignal(int) { stopping = 1; }

using Json = nlohmann::json;

auto systemTimeNanos() -> simex::common::Nanos {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

auto parseParticipantSimulator(const Json &document)
    -> simex::participant::ParticipantSimulatorConfig {
  simex::participant::ParticipantSimulatorConfig config;
  if (!document.contains("participant_simulator")) return config;

  const auto &json = document.at("participant_simulator");
  config.enabled = json.value("enabled", config.enabled);
  config.seed = json.value("seed", config.seed);
  config.client_id_start = json.value("client_id_start", config.client_id_start);
  config.market_maker_count = json.value("market_maker_count", config.market_maker_count);
  config.taker_count = json.value("taker_count", config.taker_count);
  config.orders_per_second = json.value("orders_per_second", config.orders_per_second);
  if (json.contains("fair_value_ticks") && !json.at("fair_value_ticks").is_null()) {
    config.fair_value_ticks = json.at("fair_value_ticks").get<simex::common::PriceTicks>();
  }
  config.fair_value_interval_nanos =
      json.value("fair_value_interval_nanos", config.fair_value_interval_nanos);
  config.fair_value_step_ticks = json.value("fair_value_step_ticks", config.fair_value_step_ticks);
  config.fair_value_reversion_ticks =
      json.value("fair_value_reversion_ticks", config.fair_value_reversion_ticks);
  config.quote_levels = json.value("quote_levels", config.quote_levels);
  config.quote_spread_ticks = json.value("quote_spread_ticks", config.quote_spread_ticks);
  config.quote_ttl_nanos = json.value("quote_ttl_nanos", config.quote_ttl_nanos);
  config.min_qty = json.value("min_qty", config.min_qty);
  config.max_qty = json.value("max_qty", config.max_qty);
  config.max_position = json.value("max_position", config.max_position);
  config.ticker_id = json.value("ticker_id", config.ticker_id);
  return config;
}

auto validateParticipantClientRange(const simex::participant::ParticipantSimulatorConfig &config,
                                    simex::common::ClientId external_client) -> bool {
  const auto count = static_cast<std::uint64_t>(config.market_maker_count) +
                     static_cast<std::uint64_t>(config.taker_count);
  if (!config.enabled || count == 0) return true;
  const auto first = static_cast<std::uint64_t>(config.client_id_start);
  if (first >= static_cast<std::uint64_t>(simex::common::INVALID_CLIENT_ID) ||
      count > static_cast<std::uint64_t>(simex::common::INVALID_CLIENT_ID) - first) {
    return false;
  }
  const auto last = first + count - 1;
  return static_cast<std::uint64_t>(external_client) < first ||
         static_cast<std::uint64_t>(external_client) > last;
}

auto scheduleCurrentTradingDay(const simex::common::SimexConfig &instrument_config,
                               simex::transport::ExchangeServiceConfig *service_config,
                               simex::common::Nanos now) -> void {
  simex::exchange::SessionCalendar calendar(instrument_config.session_template);
  if (!calendar.addTradingDay({service_config->runtime.trading_day,
                               service_config->runtime.reference_price,
                               true,
                               std::nullopt})) {
    throw std::runtime_error("Cannot add configured trading day to session calendar");
  }

  service_config->runtime.initial_phase = calendar.phaseAt(now);
  auto cursor = now;
  // The first version schedules the configured trading day's remaining phase
  // transitions. Adding the next trading day's holiday-aware schedule and
  // rollover belongs in a future calendar integration pass.
  for (std::size_t count = 0; count < 128; ++count) {
    const auto next = calendar.nextTransitionAfter(cursor);
    if (!next) break;
    service_config->runtime.sessions.push_back(
        {next->timestamp, next->trading_day, next->phase});
    cursor = next->timestamp;
  }
}
}

int main(int argc, char** argv) {
  try {
    const std::filesystem::path path = argc > 1 ? argv[1] : "server.json";
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open simex server configuration");
    nlohmann::json document;
    file >> document;
    const auto instrument_path = path.parent_path() / document.at("instrument_config").get<std::string>();
    simex::transport::ExchangeServiceConfig config;
    config.runtime.instrument = simex::common::loadSimexConfig(instrument_path).instrument;
    config.runtime.reference_price = document.at("reference_price").get<std::int64_t>();
    config.runtime.trading_day = document.at("trading_day").get<std::uint32_t>();
    config.runtime.clock_mode = simex::runtime::ClockMode::REALTIME;
    config.runtime.initial_time = systemTimeNanos();
    config.runtime.queue_capacity = document.value("queue_capacity", 4096U);
    const auto tcp_port = document.at("tcp_port").get<int>();
    const auto udp_port = document.at("udp_destination_port").get<int>();
    const auto client = document.at("client_id").get<std::int64_t>();
    const auto snapshot_ms = document.value("snapshot_interval_ms", 1000);
    const auto max_run_seconds = document.value("max_run_seconds", 28800);
    if (tcp_port < 1 || tcp_port > 65535 || udp_port < 1 || udp_port > 65535 ||
        client < 0 || client >= simex::common::INVALID_CLIENT_ID || snapshot_ms < 1 ||
        max_run_seconds < 1 || max_run_seconds > 86400 || config.runtime.reference_price <= 0 ||
        config.runtime.queue_capacity == 0 || config.runtime.queue_capacity > 1048576)
      throw std::runtime_error("Invalid simex server configuration");
    config.tcp.port = static_cast<std::uint16_t>(tcp_port);
    config.udp_destination_port = static_cast<std::uint16_t>(udp_port);
    config.client_id = static_cast<simex::common::ClientId>(client);
    config.runtime.external_client_id = config.client_id;
    config.runtime.participant_simulator = parseParticipantSimulator(document);
    if (!validateParticipantClientRange(config.runtime.participant_simulator, config.client_id)) {
      throw std::runtime_error("participant simulator client ID range overlaps external client");
    }
    const auto instrument_config = simex::common::loadSimexConfig(instrument_path);
    scheduleCurrentTradingDay(instrument_config, &config, config.runtime.initial_time);
    config.snapshot_interval = std::chrono::milliseconds(snapshot_ms);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    // TODO(simex-counterparty): When integrating the counterparty mechanism,
    // decide whether it runs on the runtime thread or submits through a serialized
    // command owner. It must not become a second producer on the SPSC command queue.
    simex::transport::ExchangeService service(config);
    if (!service.start()) throw std::runtime_error("Cannot start simex transport service");
    std::cout << "event=simex_ready tcp_port=" << service.tcpPort()
              << " udp_port=" << udp_port << " client_id=" << client
              << " clock=REALTIME trading_day=" << config.runtime.trading_day << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(max_run_seconds);
    while (!stopping && std::chrono::steady_clock::now() < deadline) {
      if (service.sessionEnded()) break;
      if (!service.ready()) throw std::runtime_error("Simex runtime or transport failed; restart with fresh participant state");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    service.stop();
    std::cout << "event=simex_stopped" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "event=simex_failure reason=" << error.what() << std::endl;
    return 1;
  }
}
