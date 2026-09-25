# Explicit virtual time drives trading-day rollover

The first deterministic harness uses a `Common::VirtualClock` that advances only when a scenario or replay asks it to. Trading-day boundaries are scheduled explicitly and emit `TRADING_DAY_ROLLOVER` events; the matching engine does not infer a day change from an incoming order.

**Status:** accepted

**Consequences:** SHFE night-session and holiday calendars remain a policy outside the common clock. Tests can move across a boundary without sleeping, and rollover events become part of the normalized event trace and state-hash inputs.
