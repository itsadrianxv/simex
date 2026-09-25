# Direct deterministic participant harness is the correctness path

The first correctness path drives `FIFOSequencer`, the Chapter 12 queues, and `MatchingEngine` directly. It records normalized requests, private responses, fills, public market updates, and a final state hash so that the same input can be replayed and compared without depending on TCP, UDP, sleeps, or a strategy. Private responses and public updates remain separate streams in the trace; the first version does not add a cross-stream event order.

**Status:** accepted

**Consequences:** TCP order entry and UDP market-data wiring are deferred to a later smoke-test layer. Full durable journaling and recovery are outside the first version.
