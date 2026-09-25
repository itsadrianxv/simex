# Thin front clearing stays beside the order book

Position buckets, close-today/close-yesterday availability, and order freezes are handled by a thin front-clearing boundary adjacent to the matching engine. The order book remains responsible for price-time matching; clearing validates and reserves before matching, applies fills, and releases canceled quantity, without becoming a margin or settlement engine.

**Status:** accepted

**Consequences:** Open, close-today, and close-yesterday are order attributes. An insufficient close quantity produces a deterministic whole-order rejection, with no automatic fallback between today and yesterday buckets.
