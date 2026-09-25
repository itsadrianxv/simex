# Simple immediate order semantics for the first version

The first version supports market orders, IOC, and FOK with deliberately small semantics around the Chapter 12 matcher. A market order consumes available opposite-side liquidity and cancels its remainder; IOC cancels any remainder; FOK performs a pre-check and produces no partial fill when the full quantity is unavailable. These extensions are simulator behavior, because the official SHFE pages currently recorded in `docs/shfe/official-rules.md` do not define FAK/FOK terminology or wire fields.

**Status:** accepted

**Consequences:** These order types are validated before `MEOrderBook`, never rest in the book, and need explicit private outcomes and reason codes. The auction path can remain limit-order-only until its own simulator rule is accepted.
