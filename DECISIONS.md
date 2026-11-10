# Architectural & Design Decisions Log

This document records key technical decisions, options evaluated, and the rationale behind each choice.

---

## ADR-001: Orderbook Data Structures for Prediction Market CLOB

- **Context**: Polymarket binary markets trade outcome shares priced strictly between 0.00 and 1.00 (e.g. tick sizes 0.01 or 0.001). We need an in-memory orderbook engine in C++17 to maintain bid/ask price levels with fast lookup, insertion, update, and order matching/crossing.
- **Alternatives Considered**:
  1. *`std::map<double, Level>`*: Simple, ordered traversal. Drawback: node allocation overhead, cache misses, floating point key precision issues (`0.1 + 0.2 != 0.3`).
  2. *Fixed-array / Direct-indexed array of Price Ticks*: Direct index lookup ($O(1)$). Drawback: while great for fixed 100-cent grids, binary markets with dynamic sub-cent tick sizes ($0.001$, $0.0005$) require larger tables or sparse structures.
  3. *`std::map<int64_t, PriceLevel, std::greater/less>` with Fixed-Point Ticks*: Ordered associative container using integer arithmetic.
- **Decision**: 
  - Price representation: Integer fixed-point representation with scale factor $10^4$ (`PRICE_SCALE = 10000`). For instance, $\$0.5200 \rightarrow 5200$ ticks.
  - Quantity representation: Scaled integer with factor $10^4$ (`QTY_SCALE = 10000`) to support exact fractional shares (e.g., $1500.5 \rightarrow 15005000$ units).
  - Book structure: `std::map<int64_t, PriceLevel, std::greater<int64_t>>` for bids (best bid at `.begin()`) and `std::map<int64_t, PriceLevel, std::less<int64_t>>` for asks (best ask at `.begin()`).
- **Rationale**:
  - In trading systems interviews, eliminating floating-point rounding errors in keys is essential.
  - Best-bid and best-ask queries are $O(1)$ via `.begin()`.
  - Ordered traversal makes generating top-N L2 market depth snapshots trivial and strictly ordered.

---

## ADR-002: Polymarket API Semantics & Feed Processing

- **Context**: Polymarket CLOB transmits two distinct market data message types over WebSocket:
  1. `book`: Initial orderbook snapshot with full arrays of `bids` and `asks`.
  2. `price_change`: Incremental price level updates containing `{ price, size, side }`.
- **Key Observation from Docs**: In Polymarket's `price_change` feed, `size` represents the **new aggregate replacement size**, not a cumulative delta. When `size == "0"`, the price level is completely exhausted and must be deleted from the book.
- **Decision**: 
  - `apply_snapshot`: Wipes existing state, resets timestamp and hash, and instantiates bids and asks.
  - `update_level`: Erases the key if `size <= 0`; otherwise, inserts or updates the aggregate quantity.
- **Rationale**: Direct adherence to official Polymarket CLOB semantics guarantees that the local book matches exchange state without drift.

---

## ADR-003: Matching Engine Logic & Price-Time Priority

- **Context**: Beyond passively mirroring L2 data, the engine supports active limit order placement, crossing, and execution generation.
- **Decision**:
  - Aggressive orders cross against resting opposite liquidity (BUY crosses against asks $\le$ limit price; SELL crosses against bids $\ge$ limit price).
  - Matching against L2 aggregate levels decrements aggregate volume and records anonymous executions.
  - Matching against resting limit orders uses a FIFO `std::deque<Order>` per price level (Price-Time Priority).
  - Any unfilled remainder from an aggressive limit order rests in the book on its respective side.
  - Order cancellation uses an auxiliary index `std::unordered_map<uint64_t, OrderLocation>` for $O(1)$ level lookup.
- **Rationale**: Allows the engine to serve both as a real-time market data reflector and an active matching engine simulator for backtesting and strategy evaluation.

---

## ADR-004: Quantitative Top-of-Book Analytics

- **Context**: Prediction market trading requires real-time pricing signals.
- **Decision**: Implemented native analytical calculations:
  - Spread: $\text{Best Ask} - \text{Best Bid}$.
  - Mid-Price: $\frac{\text{Best Bid} + \text{Best Ask}}{2}$.
  - Micro-Price (Volume-Weighted Mid-Price):
    $$P_{\text{micro}} = \frac{P_{\text{bid}} \cdot Q_{\text{ask}} + P_{\text{ask}} \cdot Q_{\text{bid}}}{Q_{\text{bid}} + Q_{\text{ask}}}$$
- **Rationale**: Micro-price accounts for order book imbalance (liquidity skew), providing a more responsive fair-value estimator than simple mid-price.


---

## ADR-005: WebSocket Client Selection (IXWebSocket vs Boost.Beast)

- **Context**: Phase 1 requires a robust C++ WebSocket client to stream live market data from Polymarket CLOB (`wss://ws-subscriptions-clob.polymarket.com/ws/market`), survive network drops, handle automatic ping/pong keepalives, and apply exponential backoff.
- **Alternatives Considered**:
  1. *Boost.Beast*: Industry standard in high-frequency trading shops. However, Boost.Beast is an asynchronous building block (Asio-based) rather than a complete client:
     - Requires hundreds of lines of manual state-machine boilerplate for TLS handshake, socket reconnection loops, timer cancellation, and ping/pong heartbeats.
     - Adds large compile-time overhead and Boost dependency weight.
  2. *IXWebSocket*: Lightweight C++17 WebSocket and HTTP client library designed specifically for resilient client connections:
     - Built-in automatic reconnect with configurable exponential backoff (`setAutomaticReconnection(true)`, `setMinWaitBetweenReconnectionRetries`, `setMaxWaitBetweenReconnectionRetries`).
     - Built-in automatic ping/pong background thread to keep proxy and NAT connections alive.
     - Native OpenSSL TLS support on MinGW/Windows/Linux with fast compilation and zero external baggage.
- **Decision**: Selected **IXWebSocket** with OpenSSL for the raw feed recorder.
- **Rationale**: For an ingest recorder whose primary responsibility is 100% untouched raw capture and reconnect resilience without packet loss, IXWebSocket delivers battle-tested reconnection and keepalive semantics out of the box with clear, readable code that can be easily explained and defended in an interview.


---

## ADR-006: Invariant Verification Engine & Offline Dataset Validation

- **Context**: In Phase 2, we establish baseline correctness for the L2 orderbook engine. Prediction market orderbooks have strict invariants that must never be violated during real-time streaming:
  1. *No Crossed Book*: $\text{Best Bid} < \text{Best Ask}$ whenever both sides have active liquidity.
  2. *Positive Liquidity*: Every active price level stored in the map must have strictly positive volume ($\forall \text{level}, Q > 0$). Levels depleted to $Q \le 0$ must be pruned immediately.
  3. *Strict Bid Monotonicity*: Bids in the map must be strictly descending in price ($P_i > P_{i+1}$).
  4. *Strict Ask Monotonicity*: Asks in the map must be strictly ascending in price ($P_i < P_{i+1}$).
- **Validation Method**:
  - Implemented `OrderBook::check_invariants()` returning detailed failure messages upon any invariant breach.
  - Built an offline invariant replayer (`core/src/replay_invariants.cpp`) that ingests real captured feeds line-by-line, applies snapshots and deltas, and validates all invariants on every single update.
- **Empirical Results**:
  - Replayed the 30-minute production recording (`data/polymarket_recording.jsonl`) comprising 7,738 raw events across 8 distinct outcome tokens.
  - Executed **12,992 invariant checks** across **12,982 applied level updates**.
  - **Zero invariant violations detected** (0 errors).
- **Rationale**: Proving that the engine ingests real production exchange traffic for 30 minutes without a single crossed book or lingering zero-size level provides ironclad defense in a trading systems interview.


---

## ADR-007: Desync Detection Signals, Self-Healing State Machine & Fault Injection

- **Context**: In Phase 3, we build the engine's resiliency layer to handle real-world network packet loss, out-of-order delivery, and desyncs. Because Polymarket CLOB market data lacks monotonic integer sequence numbers, traditional sequence-gap detection is impossible.
- **Signals Used for Desync Detection**:
  1. *Timestamp Regression*: Ingesting an update with `timestamp_ms < last_timestamp_ms_` signals a delayed or out-of-order delivery.
  2. *Crossed Book Invariant Breach*: An update resulting in $\text{best\_bid} \ge \text{best\_ask}$ signals dropped or misordered deltas.
  3. *Non-Positive Liquidity*: Any level corrupted to $Q \le 0$ violates structural invariants.
- **State Machine Architecture**:
  - `LIVE`: Book is synchronized and actively serving BBO and trade queries.
  - `STALE`: A desync or invariant violation occurred. Queries return stale flags; trading algorithms halt.
  - `REBUILDING`: A fresh snapshot request has been initiated (via REST or resubscription).
  - `LIVE` (Restored): Once the fresh snapshot is applied and invariants re-verified, the book resumes live operation.
- **Fault Injection & Property Testing**:
  - Created `FaultInjector` using seeded deterministic PRNG (`std::mt19937_64`) simulating 4 network fault modes: drops, duplicates, reordering, and delay/timestamp regressions.
  - Verified across multiple seeds (`42`, `101`, `777`, `1337`, `99999`) that after fault-induced divergence, triggering snapshot recovery returns the book to bit-for-bit state equality (`test_book == clean_book`) with the clean reference stream.


---

## ADR-008: Bounded Price-Tick Indexed Array vs Red-Black Tree for Prediction Market CLOB

- **Context**: In Phase 4, we evaluate data-structure optimizations against our baseline `std::map` (red-black tree) orderbook.
- **Alternatives Considered**:
  1. *`std::map<int64_t, PriceLevel>`*: Baseline. Simple, self-balancing, but incurs node pointer chasing and tree rebalancing latency spikes.
  2. *`DirectArrayOrderBook` (Flat Price-Tick Array)*: Exploits the domain invariant that Polymarket binary shares trade exclusively in the bounded domain $[0.0000, 1.0000]$. With scale $10^4$, exactly 10,001 integer slots cover all valid ticks.
  3. *`FastMatchingBook` (`std::list<Order>` with Stored Iterators)*: Replaces $O(K)$ linear deque scans with true $O(1)$ doubly linked list node erasure for order cancellation.
- **Decision & Equivalence Proof**:
  - Implemented `DirectArrayOrderBook` and verified 100% bit-for-bit equivalence against `std::map` across all 12,982 production updates (`test_equivalence.exe`).
- **Empirical Findings**:
  - `DirectArrayOrderBook` delivers **7.39M ops/sec** (vs 5.47M in `std::map`), a **35% throughput speedup**.
  - Mean latency drops from **162 ns** to **108 ns** (33.5% lower).
  - Most critically, tail latency at p99.9 collapses from **10,440 ns down to 1,720 ns** (6x lower tail latency), because flat array indexing eliminates heap node allocations and tree rebalancing rotations.


---

## ADR-009: Deterministic Replay Engine & Canonical State Hashing

- **Context**: In Phase 5, we require exact session reproduction for quantitative backtesting, bug investigation, and auditability.
- **Alternatives Considered**:
  1. *Floating-point serialization*: Outputting prices and quantities as text strings (`"0.52"`) and computing SHA-256 over text logs. Drawback: Slow, string allocation overhead, and sensitive to locale/formatting variations.
  2. *Binary FNV-1a 64-bit Canonical State Hasher*:
     - Computes a running 64-bit FNV-1a digest over raw integer ticks and units: `(best_bid_ticks, best_ask_ticks, best_bid_qty, best_ask_qty)`.
     - Canonical final book state hash: Serializes the active red-black tree bids (descending) and asks (ascending) directly from memory.
- **Decision**: Implemented `StateHasher` and `Replayer`:
  - Supports unthrottled execution (`max` speed: ~1M+ updates/sec), real-time paced replay (`1x`), and accelerated replay (`10x`).
  - Dual-layer determinism verification:
    1. *Running Output Stream Hash*: Detects any transient discrepancy, order swap, or rounding artifact during live streaming.
    2. *Canonical Book State Hash*: Detects any divergence in final resting book depth or price levels.
- **Verification & Results**:
  - Replayed the 30-minute production recording across two independent passes.
  - Running stream digest matched bit-for-bit: `0x7bf683492c841f21` in both passes across 12,992 updates.
  - Golden test (`test_golden_replay.exe`) passes against repo fixture `data/golden_sample.jsonl`.
  - Target `ninja replay-test` passes cleanly.
