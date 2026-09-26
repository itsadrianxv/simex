#include "trace/trace.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace simex::trace {
namespace {

using Json = nlohmann::json;

auto parseSide(const std::string &value) -> simex::common::Side {
  if (value == "BUY") return simex::common::Side::BUY;
  if (value == "SELL") return simex::common::Side::SELL;
  throw std::runtime_error("unknown side in trace: " + value);
}

auto parseRequestType(const std::string &value) -> simex::common::RequestType {
  if (value == "NEW") return simex::common::RequestType::NEW;
  if (value == "CANCEL") return simex::common::RequestType::CANCEL;
  throw std::runtime_error("unknown request type in trace: " + value);
}

auto parseOrderType(const std::string &value) -> simex::common::OrderType {
  if (value == "LIMIT") return simex::common::OrderType::LIMIT;
  if (value == "MARKET") return simex::common::OrderType::MARKET;
  throw std::runtime_error("unknown order type in trace: " + value);
}

auto parseTimeInForce(const std::string &value) -> simex::common::TimeInForce {
  if (value == "DAY") return simex::common::TimeInForce::DAY;
  if (value == "IOC") return simex::common::TimeInForce::IOC;
  if (value == "FOK") return simex::common::TimeInForce::FOK;
  throw std::runtime_error("unknown time in force in trace: " + value);
}

auto parsePositionEffect(const std::string &value) -> simex::common::PositionEffect {
  if (value == "OPEN") return simex::common::PositionEffect::OPEN;
  if (value == "CLOSE_TODAY") return simex::common::PositionEffect::CLOSE_TODAY;
  if (value == "CLOSE_YESTERDAY") return simex::common::PositionEffect::CLOSE_YESTERDAY;
  throw std::runtime_error("unknown position effect in trace: " + value);
}

auto parseReasonCode(const std::string &value) -> simex::common::ReasonCode {
  for (auto code : {simex::common::ReasonCode::NONE,
                    simex::common::ReasonCode::INVALID_TICK,
                    simex::common::ReasonCode::INVALID_PRICE,
                    simex::common::ReasonCode::INVALID_QTY,
                    simex::common::ReasonCode::PRICE_LIMIT_EXCEEDED,
                    simex::common::ReasonCode::REFERENCE_PRICE_UNAVAILABLE,
                    simex::common::ReasonCode::SESSION_CLOSED,
                    simex::common::ReasonCode::ORDER_TYPE_NOT_ALLOWED,
                    simex::common::ReasonCode::INSUFFICIENT_CLOSE_TODAY,
                    simex::common::ReasonCode::INSUFFICIENT_CLOSE_YESTERDAY,
                    simex::common::ReasonCode::FOK_NOT_FILLED,
                    simex::common::ReasonCode::DUPLICATE_ORDER_ID,
                    simex::common::ReasonCode::SESSION_END,
                    simex::common::ReasonCode::ORDER_NOT_FOUND,
                    simex::common::ReasonCode::QUEUE_FULL}) {
    if (simex::common::reasonCodeToString(code) == value) return code;
  }
  throw std::runtime_error("unknown reason code in trace: " + value);
}

auto parseResponseType(const std::string &value) -> simex::common::ResponseType {
  if (value == "ACCEPTED") return simex::common::ResponseType::ACCEPTED;
  if (value == "REJECTED") return simex::common::ResponseType::REJECTED;
  if (value == "CANCELED") return simex::common::ResponseType::CANCELED;
  if (value == "FILLED") return simex::common::ResponseType::FILLED;
  if (value == "CANCEL_REJECTED") return simex::common::ResponseType::CANCEL_REJECTED;
  throw std::runtime_error("unknown response type in trace: " + value);
}

auto parseMarketUpdateType(const std::string &value) -> simex::common::MarketUpdateType {
  if (value == "ADD") return simex::common::MarketUpdateType::ADD;
  if (value == "MODIFY") return simex::common::MarketUpdateType::MODIFY;
  if (value == "CANCEL") return simex::common::MarketUpdateType::CANCEL;
  if (value == "TRADE") return simex::common::MarketUpdateType::TRADE;
  throw std::runtime_error("unknown market update type in trace: " + value);
}

auto parseSessionPhase(const std::string &value) -> simex::common::SessionPhase {
  if (value == "CLOSED") return simex::common::SessionPhase::CLOSED;
  if (value == "AUCTION_SUBMIT") return simex::common::SessionPhase::AUCTION_SUBMIT;
  if (value == "AUCTION_MATCH") return simex::common::SessionPhase::AUCTION_MATCH;
  if (value == "CONTINUOUS") return simex::common::SessionPhase::CONTINUOUS;
  if (value == "BREAK") return simex::common::SessionPhase::BREAK;
  throw std::runtime_error("unknown session phase in trace: " + value);
}

auto requestJson(const simex::exchange::ClientRequest &request) -> Json {
  return {{"type", request.type == simex::common::RequestType::NEW ? "NEW" : "CANCEL"},
          {"rx_time", request.rx_time},
          {"client_id", request.client_id},
          {"ticker_id", request.ticker_id},
          {"client_order_id", request.client_order_id},
          {"side", std::string(simex::common::sideToString(request.side))},
          {"order_type", std::string(simex::common::orderTypeToString(request.order_type))},
          {"time_in_force", std::string(simex::common::timeInForceToString(request.time_in_force))},
          {"position_effect", std::string(simex::common::positionEffectToString(request.position_effect))},
          {"price_ticks", request.price_ticks},
          {"qty", request.qty}};
}

auto responseJson(const simex::exchange::ClientResponse &response) -> Json {
  return {{"type", response.type == simex::common::ResponseType::ACCEPTED
                         ? "ACCEPTED"
                         : response.type == simex::common::ResponseType::REJECTED
                               ? "REJECTED"
                               : response.type == simex::common::ResponseType::CANCELED
                                     ? "CANCELED"
                                     : response.type == simex::common::ResponseType::FILLED ? "FILLED" : "CANCEL_REJECTED"},
          {"reason", std::string(simex::common::reasonCodeToString(response.reason))},
          {"client_id", response.client_id},
          {"ticker_id", response.ticker_id},
          {"client_order_id", response.client_order_id},
          {"market_order_id", response.market_order_id},
          {"side", std::string(simex::common::sideToString(response.side))},
          {"position_effect", std::string(simex::common::positionEffectToString(response.position_effect))},
          {"price_ticks", response.price_ticks},
          {"exec_qty", response.exec_qty},
          {"leaves_qty", response.leaves_qty}};
}

auto updateJson(const simex::exchange::MarketUpdate &update) -> Json {
  return {{"type", update.type == simex::common::MarketUpdateType::ADD
                        ? "ADD"
                        : update.type == simex::common::MarketUpdateType::MODIFY
                              ? "MODIFY"
                              : update.type == simex::common::MarketUpdateType::TRADE ? "TRADE" : "CANCEL"},
          {"ticker_id", update.ticker_id},
          {"market_order_id", update.market_order_id},
          {"side", std::string(simex::common::sideToString(update.side))},
          {"price_ticks", update.price_ticks},
          {"qty", update.qty},
          {"leaves_qty", update.leaves_qty},
          {"rx_time", update.rx_time}};
}

}  // namespace

auto write(const std::filesystem::path &path, const TraceRecord &record) -> void {
  Json json{{"schema_version", record.schema_version},
            {"scenario", record.scenario},
            {"config", record.config},
            {"initial_state", {{"trading_day", record.initial_trading_day},
                                {"previous_settlement_ticks", record.previous_settlement_ticks},
                                {"phase", std::string(simex::common::sessionPhaseToString(record.initial_phase))}}},
            {"requests", Json::array()},
            {"rollovers", Json::array()},
            {"private_responses", Json::array()},
            {"public_updates", Json::array()},
            {"final_state_hash", record.final_state_hash}};
  for (const auto &request : record.requests) json["requests"].push_back(requestJson(request));
  for (const auto &rollover : record.rollovers) {
    json["rollovers"].push_back({{"timestamp", rollover.timestamp}, {"trading_day", rollover.trading_day}});
  }
  for (const auto &response : record.private_responses) json["private_responses"].push_back(responseJson(response));
  for (const auto &update : record.public_updates) json["public_updates"].push_back(updateJson(update));

  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write trace: " + path.string());
  output << json.dump(2) << '\n';
}

auto read(const std::filesystem::path &path) -> TraceRecord {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open trace: " + path.string());
  Json json;
  input >> json;

  TraceRecord record;
  record.schema_version = json.at("schema_version").get<int>();
  if (record.schema_version != 1) throw std::runtime_error("unsupported trace schema_version");
  record.scenario = json.at("scenario").get<std::string>();
  record.config = json.at("config").get<std::string>();
  record.initial_trading_day = json.at("initial_state").at("trading_day").get<simex::common::TradingDayId>();
  record.previous_settlement_ticks = json.at("initial_state").at("previous_settlement_ticks").get<simex::common::PriceTicks>();
  record.initial_phase = parseSessionPhase(json.at("initial_state").at("phase").get<std::string>());
  record.final_state_hash = json.at("final_state_hash").get<std::string>();

  for (const auto &value : json.at("requests")) {
    simex::exchange::ClientRequest request;
    request.type = parseRequestType(value.at("type").get<std::string>());
    request.rx_time = value.at("rx_time").get<simex::common::Nanos>();
    request.client_id = value.at("client_id").get<simex::common::ClientId>();
    request.ticker_id = value.at("ticker_id").get<simex::common::TickerId>();
    request.client_order_id = value.at("client_order_id").get<simex::common::ClientOrderId>();
    request.side = parseSide(value.at("side").get<std::string>());
    request.order_type = parseOrderType(value.at("order_type").get<std::string>());
    request.time_in_force = parseTimeInForce(value.at("time_in_force").get<std::string>());
    request.position_effect = parsePositionEffect(value.at("position_effect").get<std::string>());
    request.price_ticks = value.at("price_ticks").get<simex::common::PriceTicks>();
    request.qty = value.at("qty").get<simex::common::Qty>();
    record.requests.push_back(request);
  }
  for (const auto &value : json.at("rollovers")) {
    record.rollovers.push_back({value.at("timestamp").get<simex::common::Nanos>(),
                                value.at("trading_day").get<simex::common::TradingDayId>()});
  }
  for (const auto &value : json.at("private_responses")) {
    simex::exchange::ClientResponse response;
    response.type = parseResponseType(value.at("type").get<std::string>());
    response.reason = parseReasonCode(value.at("reason").get<std::string>());
    response.client_id = value.at("client_id").get<simex::common::ClientId>();
    response.ticker_id = value.at("ticker_id").get<simex::common::TickerId>();
    response.client_order_id = value.at("client_order_id").get<simex::common::ClientOrderId>();
    response.market_order_id = value.at("market_order_id").get<simex::common::MarketOrderId>();
    response.side = parseSide(value.at("side").get<std::string>());
    response.position_effect = parsePositionEffect(value.at("position_effect").get<std::string>());
    response.price_ticks = value.at("price_ticks").get<simex::common::PriceTicks>();
    response.exec_qty = value.at("exec_qty").get<simex::common::Qty>();
    response.leaves_qty = value.at("leaves_qty").get<simex::common::Qty>();
    record.private_responses.push_back(response);
  }
  for (const auto &value : json.at("public_updates")) {
    simex::exchange::MarketUpdate update;
    update.type = parseMarketUpdateType(value.at("type").get<std::string>());
    update.ticker_id = value.at("ticker_id").get<simex::common::TickerId>();
    update.market_order_id = value.at("market_order_id").get<simex::common::MarketOrderId>();
    update.side = parseSide(value.at("side").get<std::string>());
    update.price_ticks = value.at("price_ticks").get<simex::common::PriceTicks>();
    update.qty = value.at("qty").get<simex::common::Qty>();
    update.leaves_qty = value.at("leaves_qty").get<simex::common::Qty>();
    update.rx_time = value.at("rx_time").get<simex::common::Nanos>();
    record.public_updates.push_back(update);
  }
  return record;
}

auto sha256(std::string_view canonical_bytes) -> std::string {
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  if (context == nullptr) throw std::runtime_error("EVP_MD_CTX_new failed");

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  const auto cleanup = [&]() { EVP_MD_CTX_free(context); };
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context, canonical_bytes.data(), canonical_bytes.size()) != 1 ||
      EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1) {
    cleanup();
    throw std::runtime_error("SHA-256 failed");
  }
  cleanup();

  std::ostringstream output;
  output << "sha256:" << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < digest_size; ++i) output << std::setw(2) << static_cast<unsigned>(digest[i]);
  return output.str();
}

}  // namespace simex::trace
