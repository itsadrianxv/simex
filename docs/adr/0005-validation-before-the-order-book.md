# Validate orders before the Chapter 12 order book

Instrument validity, tick and price constraints, session eligibility, order-type rules, and thin-clearing availability are checked before an order reaches `MEOrderBook`. The venue adds a general `REJECTED` response with stable reason codes while retaining `CANCEL_REJECTED` for a cancel request that cannot find a live order.

**Status:** accepted

**Consequences:** `MEOrderBook` remains focused on matching and book mutation. Market-order remainders are canceled immediately and never become resting orders; the exact phase and auction rules are documented from official SHFE sources before implementation.
