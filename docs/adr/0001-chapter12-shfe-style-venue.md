# Chapter 12 mental model with one SHFE-style contract profile

The first venue implementation keeps the Chapter 12 component boundaries and single matching-engine writer, and models one configurable SHFE-style Chinese commodity-futures contract. This gives the project a concrete Chinese market profile without introducing a multi-venue abstraction before the first vertical slice is verified.

**Status:** accepted

**Consequences:** `OrderServer`, `FIFOSequencer`, `MatchingEngine`, `MEOrderBook`, `MarketDataPublisher`, and `SnapshotSynthesizer` remain the primary boundaries. Instrument metadata and session rules are added around the existing flow and can later support more contract profiles.
