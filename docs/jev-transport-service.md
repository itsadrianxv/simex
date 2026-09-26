# Local transport service for Jev

Build `simex_server` with the normal CMake build and run it with `server.json`:

```sh
cmake -S . -B build/jev-integration -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/jev-integration -j 4
./build/jev-integration/simex_server server.json
```

The server loads the instrument profile relative to server.json, starts REALTIME in a continuous session on the configured trading day, and binds localhost TCP. It publishes UDP to the configured destination port and emits `event=simex_ready` when the service starts. It accepts one participant connection per process. SIGINT/SIGTERM or peer disconnection ends the service; start a fresh process for another run.

The initial participant is empty. No liquidity generator, background counterparty, market-data scenario, account recovery, or day-rollover scenario is added by this integration. Those are separate work. Keep the run within one trading day. `max_run_seconds` bounds the server lifetime and may not exceed 86400.

The Jev adapter shares the v1 protocol definitions directly. V1 is a native little-endian 64-bit Linux ABI, now checked by size/offset assertions. Relative protocol-header includes prevent collisions with the participant repository's similarly named common/types.h. The payload format has not changed.

Companion fixes required for the independent process integration:

- Atomic queue publication prevents data races between runtime and transport threads; each queue still requires one producer and one consumer.
- A mutex protects snapshot synthesis against concurrent runtime updates. Output overflow fails the runtime instead of silently dropping events and breaking sequence correspondence.
- Nonblocking TCP framing handles partial reads/writes, sends asynchronous responses without another request, and allows stop with an idle connected participant. Reconnection/replay is not implemented.
- The configured client id selects private responses. Responses for internally generated counterparties are not sent to Jev. Receive timestamps for REALTIME orders come from the runtime clock.
- UDP checks send results and rejects oversized snapshots rather than silently exceeding the datagram limit. Increment draining uses bounded batches. The executable monitors failures and exits.

Existing service regression checks cover a fragmented request, a later session-end cancellation delivered while the client sends nothing, and shutdown while the client socket remains open. The runtime check also exercises concurrent bounded-queue slot reuse. The participant live runner is documented in `../jev-quant/docs/simex-venue.md`.
