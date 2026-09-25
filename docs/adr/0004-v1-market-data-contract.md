# Chapter 12 style market data without recovery guarantees

The first market-data contract preserves the Chapter 12 split between a venue-wide UDP incremental stream and an independent snapshot stream. Incremental sequence numbers are venue-wide and snapshots may report the last incremental sequence, but the first version does not promise TCP recovery, `fromVersion`, or an atomic snapshot/incremental handoff.

**Status:** accepted

**Consequences:** Public updates retain book and trade semantics while hiding participant identity. The private response stream and public market-data stream stay independent. Recovery and stronger handoff semantics can be added later without replacing the matching-engine-to-publisher mental model.
