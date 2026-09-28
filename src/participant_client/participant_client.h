#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "exchange/messages.h"
#include "market_data/snapshot_synthesizer.h"

namespace simex::participant {

struct Endpoint final {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
};

struct ClientConfig final {
  Endpoint tcp;
  Endpoint udp;
  simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
  simex::common::TickerId ticker_id = 0;
  std::size_t queue_capacity = 1024;
};

enum class SubmitError : std::uint8_t {
  NONE,
  NOT_CONNECTED,
  INVALID_REQUEST,
  QUEUE_FULL,
};

struct SubmitResult final {
  bool accepted_for_send = false;
  SubmitError error = SubmitError::NONE;
};

class RequestBuilder final {
 public:
  static auto limit(simex::common::ClientOrderId order_id,
                    simex::common::Side side,
                    simex::common::PriceTicks price_ticks,
                    simex::common::Qty qty) -> RequestBuilder;
  static auto cancel(simex::common::ClientOrderId order_id) -> RequestBuilder;

  auto timeInForce(simex::common::TimeInForce value) -> RequestBuilder &;
  auto positionEffect(simex::common::PositionEffect value) -> RequestBuilder &;

 private:
  friend class ParticipantClient;
  simex::exchange::ClientRequest request_{};
};

using Event = std::variant<simex::exchange::ClientResponse,
                           simex::exchange::MarketUpdate,
                           simex::exchange::Snapshot>;

class ParticipantClient final {
 public:
  explicit ParticipantClient(ClientConfig config);
  ~ParticipantClient();
  ParticipantClient(const ParticipantClient &) = delete;
  auto operator=(const ParticipantClient &) -> ParticipantClient & = delete;

  auto connect() -> bool;
  auto close() -> void;
  [[nodiscard]] auto connected() const noexcept -> bool;
  auto submit(const RequestBuilder &request) -> SubmitResult;
  auto poll() -> std::optional<Event>;
  auto wait(std::chrono::milliseconds timeout) -> std::optional<Event>;

 private:
  class Impl;
  Impl *impl_ = nullptr;
};

}  // namespace simex::participant
