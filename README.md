# simex

Simex is a C++ simulated exchange with an SHFE-style RB futures profile. The
first vertical slice keeps the Chapter 12 component boundaries and provides:

- deterministic virtual time, session-calendar phase events, and trading-day rollover;
- RB auction and continuous price-time matching with price limits;
- today/yesterday thin front clearing and daily-close cancellation;
- direct `FIFOSequencer`/queue/`MatchingEngine` participant harness;
- venue-sequenced in-memory incremental market data and independent snapshots;
- UTF-8 JSON replay traces with a final SHA-256 state hash.

The root [`simex.json`](simex.json) is the default profile. Official SHFE facts
and simulator-specific choices are documented in
[`docs/shfe/official-rules.md`](docs/shfe/official-rules.md).

## Build

The build prefers a system `nlohmann_json` package and requires system OpenSSL.
If the JSON package is unavailable, opt into the pinned source fallback:

```sh
cmake -S . -B build -DJEV_FETCH_NLOHMANN_JSON=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The default keeps the fetch option off and fails during CMake configuration when
the system JSON package is missing. TCP/UDP transport remains a later adapter;
the current correctness path is entirely in process.

Run the deterministic demo with:

```sh
./build/simex_demo simex.json
```
