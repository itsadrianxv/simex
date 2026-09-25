# JSON is the first replay-trace format

The participant harness records deterministic scenarios as UTF-8 JSON using `nlohmann/json`. A trace contains normalized requests, explicit rollover inputs, separate private-response and public-update streams, and a final state hash; it does not add a cross-stream order or act as a durable recovery journal.

**Status:** accepted

**Consequences:** Traces are inspectable and easy to diff. JSON parsing stays in harness/replay tooling and outside the matching hot path; a later binary format can be added without changing venue semantics.
