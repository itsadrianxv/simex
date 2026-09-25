# Session calendar supplies virtual-clock events

Instrument-specific trading schedules live in a `SessionCalendar` boundary. It translates the RB schedule, auction windows, breaks, daily close, and night-session trading-day ownership into explicit events for `Common::VirtualClock`; the clock itself only advances time and emits scheduled rollover events.

**Status:** accepted

**Consequences:** Calendar changes and holiday handling do not require changing the deterministic clock. Session transitions can be replayed from the same virtual timestamps.
