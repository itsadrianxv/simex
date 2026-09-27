# Simex venue domain

Simex is a venue-side simulation of an SHFE-style Chinese commodity futures market. This context records the market language and boundaries that the project uses while extending the Chapter 12 exchange mental model.

## Venue and participants

**Venue**:
The simulated exchange that accepts participant orders, applies session and instrument rules, matches orders, and publishes public market data.

**Participant**:
A simulated market member that submits orders and receives private order responses and public market data.
_Avoid_: Client when referring to the participant as a business actor; `client_id` remains a wire-level identifier.

**Participant harness**:
A deterministic test participant used to drive exchange scenarios and inspect private responses, fills, book state, and public market data.
_Avoid_: Strategy; the harness validates venue behavior and does not make trading decisions.

## Market state

**Instrument**:
A futures contract with its own tick size, price limits, session schedule, and position rules.
_Avoid_: Ticker when discussing the domain; `ticker_id` remains the Chapter 12 implementation identifier.

**Instrument profile**:
The configuration that names an instrument and supplies its contract size, tick size, price-band basis, session template, and order policy. The first profile is `shfe_rb_demo` in the root `simex.json`.

**Trading session**:
A named period in which an instrument accepts a defined set of order actions, such as auction submission, auction matching, continuous trading, break, or closed. The first RB profile uses the official SHFE time windows recorded in `docs/shfe/official-rules.md`.

**Trading day**:
The exchange business date to which a contract session and its positions belong. A night session may occur before the daytime portion of the same trading day.

**Call auction**:
A session in which eligible orders accumulate and are resolved by a single-price matching rule instead of continuous price-time matching. Simex selects a deterministic price using maximum volume, minimum unmatched quantity, distance to the reference price, and a fixed lower-price tie-break; unmatched limit orders carry into continuous trading with their recorded `rx_time`.

**Continuous matching**:
The phase in which orders are matched immediately by price priority and `rx_time` priority. Simex computes each trade price as the middle value of the buy price, sell price, and previous trade price.

**Price limit**:
The permitted price interval for an instrument during a trading day. An order outside the interval is rejected before it reaches the book. The first RB profile uses the previous settlement price plus or minus three percent.

**Pricetick**:
The minimum legal price increment for an instrument. Prices are represented as integer ticks and must be aligned to the instrument's tick size.

## Orders and positions

**Order book**:
The resting buy and sell orders for one instrument, ordered by price and then FIFO priority.

**Limit order**:
An order with an explicit legal price that may rest in the order book after matching.

**Market order**:
An order without a resting limit price that consumes available opposite-side liquidity according to the instrument's market-order rule.

**IOC order**:
An order that executes whatever quantity is immediately available and cancels its remainder.

**FOK order**:
An order that executes only when its full quantity can be filled immediately; otherwise it is rejected or canceled without a partial fill.

**Time in force**:
The lifetime rule attached to an order: `DAY` may rest for the trading day, `IOC` cancels its remainder immediately, and `FOK` requires a complete immediate fill.

**Position effect**:
The instruction attached to an order that declares whether a fill opens a position, closes today's position, or closes yesterday's position.

**Replace**:
A participant operation represented by canceling the old order and submitting a new order. The new order receives a new exchange order identity and FIFO priority.

**Order validation**:
The venue checks order fields, instrument constraints, session eligibility, and front-clearing availability before an order reaches `MEOrderBook`.

**Rejected order**:
An order that is refused before becoming a live book order, with a stable reason code explaining the rule that failed.

**Cancel rejected**:
A cancellation request that cannot find a cancelable live order. It is distinct from rejecting the original order.

**Open**:
An order instruction that creates a new position when it trades.

**Close today**:
An explicit close instruction that consumes the participant's position opened during the current trading day.

**Close yesterday**:
An explicit close instruction that consumes the participant's position carried from an earlier trading day.

**Thin front clearing**:
The venue-side position reservation and release needed to validate open, close-today, and close-yesterday orders and to maintain today/yesterday buckets. It excludes full margin, credit, and settlement systems.

**Order intent**:
The open/close instruction attached to an order. `Open`, `Close today`, and `Close yesterday` are order attributes rather than separate order kinds.

**Participant account**:
The single position and freeze ledger identified by a participant's `client_id` in the first version. Subaccounts are outside the first version.

## Data flow

**Order response**:
An order-specific acknowledgement, rejection, cancellation, or fill delivered only to the submitting participant.

**Incremental Stream**:
The public UDP stream of ordered book and trade updates emitted as the matching state changes.

**Snapshot**:
An independent public representation of the current book produced by `SnapshotSynthesizer`; the first version does not promise a recovery or atomic handoff protocol.

**Market event sequence**:
The order assigned to accepted venue events and public incremental updates. The first version uses the Chapter 12 `rx_time` ordering rule; replay supplies the recorded receive times and uses the resulting normalized event stream and state hash for repeatability.

**Market-order remainder**:
The unfilled quantity of a market order after available opposite-side liquidity is consumed. It is canceled immediately and never rests in the order book.

**Auction remainder**:
The quantity left after a call-auction clearing point. Whether it carries into continuous trading and how it retains priority is an explicit simulator policy when the official source does not settle the detail.

**Reason code**:
A stable machine-readable explanation attached to a rejected order or cancellation outcome, such as an invalid tick, closed session, insufficient close quantity, or unfilled FOK.

**Session calendar**:
The instrument-specific schedule that maps virtual timestamps to trading phases, breaks, auction windows, and trading-day boundaries. It supplies events to `VirtualClock` rather than being embedded in the clock.

**Phase override**:
A configuration that pins the venue to a single trading phase for an entire run, suspending scheduled session transitions. The first override value pins continuous matching so experiments see an always-open book. _Avoid_: Run mode; the override changes session state only, and clock behavior remains separate.

**Normalized event trace**:
The UTF-8 JSON replay record containing canonical requests, virtual-time events, private responses, public market updates, and the final state hash. Private responses and public updates remain separate streams and are recorded in their own production order; the first version does not invent a cross-stream ordering. It is a test and verification artifact, not a network capture or durable recovery journal.

**Virtual time**:
A deterministic timestamp supplied by a scenario or replay instead of wall-clock time. It lets session transitions and event ordering be reproduced without sleeps.

**Trading-day rollover**:
An explicit event that closes one trading day and activates the next day's calendar and position buckets. It is driven by virtual time rather than by order arrival.

**Reference price**:
The previous settlement price used to derive an instrument's daily price band. If it is unavailable, price-limited orders are rejected with `REFERENCE_PRICE_UNAVAILABLE`.

**Daily close**:
The end of the final continuous trading interval. Simex cancels live orders and releases their freezes at daily close while retaining positions until trading-day rollover.

**Trading-day schedule**:
An explicit configuration supplied by a scenario or replay that defines the RB session windows, breaks, daily close, and rollover. The first version does not call an online holiday service.
