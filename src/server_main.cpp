#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>

#include "common/config.h"
#include "transport/exchange_service.h"

namespace {
volatile std::sig_atomic_t stopping = 0;
void onSignal(int) { stopping = 1; }
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
    config.runtime.initial_phase = simex::common::SessionPhase::CONTINUOUS;
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
