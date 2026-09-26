#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/types.h"
#include "exchange/messages.h"

namespace simex::participant {

/// The small set of knobs needed to produce a live first-version market.
///
/// The simulator deliberately models only LIMIT/DAY/OPEN orders.  More
/// elaborate order plans, auction participation, and position closing are
/// left for later versions of the participant module.
struct ParticipantSimulatorConfig final {
  bool enabled = false;
  std::uint64_t seed = 1;
  simex::common::ClientId client_id_start = 1000;
  std::size_t market_maker_count = 1;
  std::size_t taker_count = 1;
  std::uint32_t orders_per_second = 1;

  /// If INVALID_PRICE_TICKS, the reference price passed to the constructor
  /// (or setReferencePrice) is used as the fair value anchor.
  simex::common::PriceTicks fair_value_ticks = simex::common::INVALID_PRICE_TICKS;
  /// A fair value update is made at most once per this interval.  Keeping the
  /// interval explicit makes REALTIME and MANUAL runs deterministic with the
  /// same sequence of tick timestamps.
  simex::common::Nanos fair_value_interval_nanos = 1'000'000'000;
  simex::common::PriceTicks fair_value_step_ticks = 1;
  simex::common::PriceTicks fair_value_reversion_ticks = 1;

  std::size_t quote_levels = 1;
  simex::common::PriceTicks quote_spread_ticks = 2;
  simex::common::Nanos quote_ttl_nanos = 5'000'000'000;
  simex::common::Qty min_qty = 1;
  simex::common::Qty max_qty = 1;
  simex::common::Qty max_position = 10;
  simex::common::TickerId ticker_id = 0;
};

struct SimulatorStats final {
  std::uint64_t generated_requests = 0;
  std::uint64_t generated_new_requests = 0;
  std::uint64_t generated_cancel_requests = 0;
  std::uint64_t accepted_orders = 0;
  std::uint64_t rejected_orders = 0;
  std::uint64_t canceled_orders = 0;
  std::uint64_t cancel_rejected_orders = 0;
  std::uint64_t fills = 0;
  std::uint64_t filled_qty = 0;
  std::size_t pending_orders = 0;
  std::size_t live_orders = 0;
  simex::common::Qty long_position = 0;
  simex::common::Qty short_position = 0;
};

/// Deterministic internal order-flow generator.
///
/// ParticipantSimulator is intentionally independent of ExchangeRuntime.  A
/// runtime thread calls tick(), submits the returned requests, and routes the
/// resulting private responses and public market updates back through the two
/// callback methods.  This keeps the simulator from becoming a second
/// producer of the runtime's SPSC request queue.
/// TODO(participant): move request production behind a serialized MPSC/runtime
/// command seam when participants are allowed to run on independent threads.
class ParticipantSimulator final {
 public:
  using RequestList = std::vector<simex::exchange::ClientRequest>;

  ParticipantSimulator(simex::common::InstrumentConfig instrument,
                       ParticipantSimulatorConfig config,
                       simex::common::PriceTicks reference_price =
                           simex::common::INVALID_PRICE_TICKS);

  ParticipantSimulator(const ParticipantSimulator &) = delete;
  auto operator=(const ParticipantSimulator &) -> ParticipantSimulator & = delete;
  ParticipantSimulator(ParticipantSimulator &&) = delete;
  auto operator=(ParticipantSimulator &&) -> ParticipantSimulator & = delete;
  ~ParticipantSimulator() = default;

  /// Set the previous settlement used for the price band and fair value.
  /// Returns false for a non-positive reference price or invalid instrument
  /// tick configuration.
  auto setReferencePrice(simex::common::PriceTicks reference_price) -> bool;

  /// Generate a bounded batch of internal requests due at `now`.
  /// Requests are generated only during CONTINUOUS.  In all other phases the
  /// returned vector is empty; the matching engine remains responsible for
  /// closing/canceling its live orders at the session boundary.
  auto tick(simex::common::Nanos now, simex::common::SessionPhase phase) -> RequestList;

  /// Feed one private response.  Responses are the source of truth for the
  /// simulator's pending/live order state and positions.
  auto onResponse(const simex::exchange::ClientResponse &response) -> void;

  /// Feed one public incremental update and maintain a lightweight BBO view.
  auto onMarketUpdate(const simex::exchange::MarketUpdate &update) -> void;

  [[nodiscard]] auto stats() const noexcept -> SimulatorStats;
  [[nodiscard]] auto bestBid() const noexcept -> std::optional<simex::common::PriceTicks>;
  [[nodiscard]] auto bestAsk() const noexcept -> std::optional<simex::common::PriceTicks>;
  [[nodiscard]] auto fairValue() const noexcept -> simex::common::PriceTicks {
    return fair_value_ticks_;
  }
  [[nodiscard]] auto lowerPriceLimit() const noexcept -> simex::common::PriceTicks {
    return lower_price_limit_;
  }
  [[nodiscard]] auto upperPriceLimit() const noexcept -> simex::common::PriceTicks {
    return upper_price_limit_;
  }
  [[nodiscard]] auto config() const noexcept -> const ParticipantSimulatorConfig & {
    return config_;
  }

 private:
  enum class BotRole : std::uint8_t { MARKET_MAKER, TAKER };
  enum class OrderState : std::uint8_t { PENDING, LIVE, CANCEL_PENDING };

  struct ActiveOrder final {
    simex::exchange::ClientRequest request;
    simex::common::Qty leaves_qty = 0;
    simex::common::Nanos submitted_at = 0;
    OrderState state = OrderState::PENDING;
    bool maker_quote = false;
  };

  struct Bot final {
    BotRole role = BotRole::MARKET_MAKER;
    simex::common::ClientId client_id = simex::common::INVALID_CLIENT_ID;
    simex::common::ClientOrderId next_order_id = 1;
    std::vector<ActiveOrder> orders;
    // OPEN orders consume gross direction-specific capacity.  A later
    // position-closing strategy may net these buckets, but v1 keeps the two
    // sides independent.
    simex::common::Qty long_position = 0;
    simex::common::Qty short_position = 0;
  };

  struct PublicOrder final {
    simex::common::Side side = simex::common::Side::BUY;
    simex::common::PriceTicks price_ticks = simex::common::INVALID_PRICE_TICKS;
    simex::common::Qty leaves_qty = 0;
  };

  auto validateConfiguration() const -> void;
  auto updateFairValue(simex::common::Nanos now) -> void;
  auto generateMakerRequests(simex::common::Nanos now, RequestList &requests) -> void;
  auto generateTakerRequest(simex::common::Nanos now, RequestList &requests) -> void;
  auto scheduleExpiredMakerCancels(simex::common::Nanos now, RequestList &requests) -> void;
  auto makeNewRequest(Bot &bot, simex::common::Side side,
                      simex::common::PriceTicks price,
                      simex::common::Qty qty, simex::common::Nanos now,
                      bool maker_quote) -> simex::exchange::ClientRequest;
  auto makeCancelRequest(Bot &bot, ActiveOrder &order,
                         simex::common::Nanos now) -> simex::exchange::ClientRequest;
  auto findBot(simex::common::ClientId client_id) -> Bot *;
  auto findBot(simex::common::ClientId client_id) const -> const Bot *;
  auto findOrder(Bot &bot, simex::common::ClientOrderId order_id) -> ActiveOrder *;
  auto findOrder(const Bot &bot, simex::common::ClientOrderId order_id) const
      -> const ActiveOrder *;
  auto removeOrder(Bot &bot, simex::common::ClientOrderId order_id) -> void;
  auto pendingQuantity(const Bot &bot, simex::common::Side side) const noexcept
      -> simex::common::Qty;
  auto hasMakerQuote(const Bot &bot, simex::common::Side side, std::size_t level) const noexcept
      -> bool;
  auto quotePrice(simex::common::Side side, std::size_t level) const noexcept
      -> simex::common::PriceTicks;
  auto alignPrice(simex::common::PriceTicks price) const noexcept
      -> simex::common::PriceTicks;
  auto clampPrice(simex::common::PriceTicks price) const noexcept
      -> simex::common::PriceTicks;
  auto randomQuantity() -> simex::common::Qty;
  auto canOpen(const Bot &bot, simex::common::Side side,
               simex::common::Qty qty) const noexcept -> bool;
  auto addPosition(Bot &bot, simex::common::Side side,
                   simex::common::Qty quantity) noexcept -> void;
  auto aggregatePosition(simex::common::Side side) const noexcept -> simex::common::Qty;

  simex::common::InstrumentConfig instrument_;
  ParticipantSimulatorConfig config_;
  std::vector<Bot> bots_;
  std::unordered_map<simex::common::MarketOrderId, PublicOrder> public_orders_;
  std::mt19937_64 random_;
  simex::common::PriceTicks reference_price_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks fair_value_ticks_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks lower_price_limit_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::PriceTicks upper_price_limit_ = simex::common::INVALID_PRICE_TICKS;
  simex::common::Nanos next_request_time_ = 0;
  simex::common::Nanos last_fair_value_update_ = 0;
  bool fair_value_initialized_ = false;
  bool reseed_quotes_ = true;
  SimulatorStats stats_;
};

}  // namespace simex::participant
