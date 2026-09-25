# Deterministic replay trace

A trace is a small UTF-8 JSON file produced by the participant harness. It records enough deterministic input and output to replay one scenario without TCP, UDP, sleeps, or a strategy. It is not a production journal, a market-data recovery protocol, or a cross-stream event log.

The first schema uses separate arrays for the input and the two output streams:

```json
{
  "schema_version": 1,
  "scenario": "rb_fifo_partial_fill",
  "config": "simex.json",
  "initial_state": {
    "trading_day": 20260925,
    "previous_settlement_ticks": 3500,
    "phase": "CONTINUOUS"
  },
  "requests": [
    {
      "rx_time": 1000000,
      "client_id": 1,
      "ticker_id": 0,
      "client_order_id": 10,
      "side": "BUY",
      "order_type": "LIMIT",
      "time_in_force": "DAY",
      "position_effect": "OPEN",
      "price_ticks": 3499,
      "qty": 2
    }
  ],
  "rollovers": [],
  "private_responses": [],
  "public_updates": [],
  "final_state_hash": "sha256:..."
}
```

`requests` are replayed in their recorded `rx_time` order. `rollovers` are explicit `VirtualClock` inputs. `private_responses` and `public_updates` preserve the production order of their own streams and are compared independently; the schema intentionally does not assign an ordering between those arrays. The harness may omit expected output arrays when it is creating a new trace, then populate them after a verified run.

The `initial_state.phase` field seeds the first session phase. The harness can
schedule explicit rollover inputs and replay them through `VirtualClock`; phase
events come from the configured `SessionCalendar` schedule.

The replay tool may use `nlohmann/json` to read and write this format. Runtime
matching code consumes typed C++ messages after parsing; JSON parsing is outside
the hot path. `ParticipantHarness::replayTrace()` feeds requests through one
sequencer batch, drains both output streams, and compares the final state hash.
