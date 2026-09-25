# Price reference is required before price-band validation

The venue validates integer-tick prices and daily price limits before an order reaches `MEOrderBook`. The RB profile derives its band from the previous settlement price; when that reference is absent, the order is rejected with `REFERENCE_PRICE_UNAVAILABLE` rather than using the latest trade as an implicit substitute.

**Status:** accepted

**Consequences:** A scenario must seed or roll forward the settlement reference before it can submit price-limited orders. End-of-day cancellation releases order freezes while position state survives until rollover.
