# Orthogonal order attributes and SHFE-style continuous price

The first order model separates `OrderType` (`LIMIT` or `MARKET`), `TimeInForce` (`DAY`, `IOC`, or `FOK`), and `PositionEffect` (`OPEN`, `CLOSE_TODAY`, or `CLOSE_YESTERDAY`). The supported first-version combinations are limit day, limit IOC, limit FOK, and market day. Continuous trades use price/time priority and the middle value of buy price, sell price, and previous trade price, matching the SHFE rule recorded in `docs/shfe/official-rules.md`.

**Status:** accepted

**Consequences:** Market orders are immediate and never rest. FOK performs a preflight liquidity and clearing check before changing state. Position effects are evaluated independently for both sides of a trade.
