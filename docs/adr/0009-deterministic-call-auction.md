# Deterministic call-auction policy is a simex rule

The official sources currently recorded for this project define the RB auction windows and general price/time matching, but do not publish a complete rule for auction price selection, unmatched order carryover, or FAK/FOK terminology. Simex therefore uses a documented simulator policy: select the price by maximum executable quantity, minimum unmatched quantity, distance to the reference price, then a fixed lower-price tie-break; match eligible orders by price and recorded `rx_time`, and carry unmatched limit orders into continuous trading.

**Status:** accepted

**Consequences:** These choices are labeled as simulator behavior in `docs/shfe/official-rules.md` and can be revised without claiming to change an official SHFE rule.
