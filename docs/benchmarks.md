# Polymarket Orderbook Engine: Empirical Benchmark Results

This document records measured benchmark metrics comparing orderbook data structures and order cancellation algorithms on real Polymarket production market data.

> [!NOTE]
> **Measurement Methodology**:
> - Replayed all 12,982 real-world delta updates pre-loaded into contiguous RAM to isolate CPU cache and algorithmic throughput from disk and JSON overhead.
> - Conducted 5 independent trials with hardware cache warmup.
> - High-resolution hardware timer (`std::chrono::high_resolution_clock`).
> - Script: `.\build\bench_comparison.exe data/polymarket_recording.jsonl`

---

## 1. Environment & Hardware Specifications

- **Operating System**: Windows 11 (x86_64)
- **Compiler**: MinGW-w64 GCC 15.2.0
- **Compilation Flags**: `-Wall -Wextra -Wpedantic -O2 -std=c++17`
- **Data Source**: 30-minute raw capture (`data/polymarket_recording.jsonl`, 7,738 messages, 12,982 L2 updates across 8 tokens)

---

## 2. Orderbook Data Structure Benchmark (Level Updates)

Comparison between **Baseline `std::map`** (Red-Black Tree) and **Optimized `DirectArrayOrderBook`** (Price-Tick Indexed Flat Array):

| Implementation | Mean Throughput (ops/sec) | Variance / StdDev | Mean Latency | Median (p50) | p99 Latency | p99.9 Latency |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline `std::map` (RB-Tree)** | 5,474,707 ops/s | $\pm$ 1,147,113 | 162.32 ns | 100.00 ns | 460.00 ns | 10,440.00 ns |
| **`DirectArrayOrderBook` (Flat Array)** | **7,393,280 ops/s** | $\pm$ 1,693,272 | **108.02 ns** | **100.00 ns** | **280.00 ns** | **1,720.00 ns** |
| **Improvement** | **+35.0% higher throughput** | — | **33.5% lower latency** | Identical | **39.1% lower p99** | **6.07x lower p99.9** |

---

## 3. Order-Level Cancellation Benchmark ($K=100$ orders/level)

Comparing order cancellation under high queue depth ($K = 100$ resting orders per level, 50,000 orders canceled):

| Cancellation Strategy | Algorithm Complexity | Mean Latency per Cancel | Throughput |
| :--- | :--- | :--- | :--- |
| **Baseline `std::deque`** | $O(K)$ linear scan | 102.31 ns | 9,773,833 cancels/s |
| **Optimized `std::list` + Iterator Map** | True $O(1)$ node pointer erasure | 113.33 ns | 8,823,789 cancels/s |

### Interview Trade-off Analysis:
* **Why `std::deque` is fast at moderate depth ($K \le 100$)**:
  `std::deque` allocates contiguous memory chunks. For small to moderate queues, iterating through contiguous memory benefits from CPU L1/L2 prefetching and cache-line spatial locality, outperforming single node pointer dereferences.
* **Why `std::list` with stored iterators wins in high-frequency cancellation regimes ($K \gg 1000$)**:
  As queue depth $K$ grows into hundreds or thousands of resting orders, `std::deque`'s scan time degrades linearly $O(K)$, while `std::list` maintains a strict, bounded **true $O(1)$** cancellation time without element shifting.

---

## 4. Why the Price-Tick Indexed Array Wins (2-Minute Interview Defense)

1. **Zero Heap Allocations on Update**:
   - In `std::map`, inserting or deleting a price level requires dynamic node allocation (`new` / `delete`) and red-black tree rebalancing rotations.
   - In `DirectArrayOrderBook`, prediction market prices are strictly bounded in $[0.0000, 1.0000]$. A fixed array of 10,001 integer slots covers every tick. Updating a price level is a single CPU write: `array[price_ticks] = new_size`.
2. **Elimination of Tail Latency Spikes (p99.9)**:
   - Notice the p99.9 tail latency: `std::map` spikes to **10,440 ns**, whereas `DirectArrayOrderBook` peaks at only **1,720 ns** (**6.07x improvement**). The spike in `std::map` occurs during deep tree rebalancing and cache misses on cold pointer nodes.
3. **L1 Spatial Locality near Top-of-Book**:
   - In prediction markets, trading activity is heavily concentrated near the spread (e.g., $0.08$ to $0.10$).
   - Scanning for the next best bid or ask touches adjacent memory offsets that are already loaded into the CPU's 64-byte cache lines.
