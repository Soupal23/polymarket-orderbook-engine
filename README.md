# Real-Time Orderbook Engine for Prediction Markets (Polymarket CLOB)

<p>
  <img src="https://img.shields.io/badge/C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake" />
  <img src="https://img.shields.io/badge/OpenSSL-721412?style=for-the-badge&logo=openssl&logoColor=white" alt="OpenSSL" />
  <img src="https://img.shields.io/badge/Node.js-339933?style=for-the-badge&logo=nodedotjs&logoColor=white" alt="Node.js" />
  <img src="https://img.shields.io/badge/Express-000000?style=for-the-badge&logo=express&logoColor=white" alt="Express" />
  <img src="https://img.shields.io/badge/Socket.IO-010101?style=for-the-badge&logo=socketdotio&logoColor=white" alt="Socket.IO" />
  <img src="https://img.shields.io/badge/React_18-20232A?style=for-the-badge&logo=react&logoColor=61DAFB" alt="React 18" />
  <img src="https://img.shields.io/badge/Vite-646CFF?style=for-the-badge&logo=vite&logoColor=white" alt="Vite" />
  <img src="https://img.shields.io/badge/Recharts-22B5BF?style=for-the-badge&logo=chartdotjs&logoColor=white" alt="Recharts" />
  <img src="https://img.shields.io/badge/Redis-DC382D?style=for-the-badge&logo=redis&logoColor=white" alt="Redis" />
  <img src="https://img.shields.io/badge/MongoDB-47A248?style=for-the-badge&logo=mongodb&logoColor=white" alt="MongoDB" />
  <img src="https://img.shields.io/badge/Docker-2496ED?style=for-the-badge&logo=docker&logoColor=white" alt="Docker" />
</p>

A high-performance, deterministic C++17 Limit Order Book (CLOB) engine, self-healing market-data pipeline, Node.js WebSocket gateway, and real-time React trading dashboard built around Polymarket prediction market feeds.

---

## System Architecture

```mermaid
flowchart LR
    subgraph External["Polymarket CLOB APIs"]
        P_WS["WebSocket Feed\nwss://ws-subscriptions-clob.polymarket.com/ws/market"]
        P_REST["REST Snapshot\n/book?token_id=..."]
    end

    subgraph Core["Core C++17 Engine (/core)"]
        REC["RawRecorder\n(IXWebSocket + OpenSSL)"]
        REP["Deterministic Replayer\n(1x / 10x / Max + FNV-1a)"]
        OB["OrderBook & DirectArrayOrderBook\n(Fixed-Point 10^4 + Invariants)"]
        HEAL["Self-Healing & FaultInjector\n(LIVE / STALE / REBUILDING)"]
        IPC["TcpPublisher\n(127.0.0.1:9001, TCP_NODELAY)"]

        P_WS --> REC --> OB
        REP --> OB
        P_REST --> HEAL --> OB
        OB --> IPC
    end

    subgraph Gateway["Node.js Gateway (/gateway)"]
        CLI["CoreClient\n(Auto-Reconnect NDJSON)"]
        STATE["StateStore\n(Redis / In-Memory Fallback)"]
        QUEUE["PersistenceWorker\n(BullMQ + MongoDB TTL / Ring Buffer)"]
        IO["Broadcaster\n(Socket.IO 50ms Coalescing)"]

        IPC -->|NDJSON Stream| CLI
        CLI --> STATE
        CLI --> QUEUE
        CLI --> IO
    end

    subgraph UI["Frontend (/dashboard)"]
        DASH["React + Vite + Recharts Terminal\n(L2 Ladder, Depth Curve, Price Dynamics, Fault Trigger)"]
    end

    IO -->|Socket.IO| DASH
```

---

## Key Features & Engineering Highlights

- **Fixed-Point Arithmetic (`10^4` scaling)**: Eliminates IEEE-754 floating-point key drift (`PRICE_SCALE = 10000`, `QTY_SCALE = 10000`).
- **Dual Book Implementations**:
  - `OrderBook`: Baseline Red-Black Tree (`std::map`) with matching engine and quantitative signals (`spread`, `mid_price`, `micro_price`, `book_imbalance`).
  - `DirectArrayOrderBook`: Flat 10,001-slot price-tick array exploiting Polymarket's bounded `[0.0000, 1.0000]` domain for $O(1)$ indexing and L1 cache locality.
  - `FastMatchingBook`: $O(1)$ doubly linked list (`std::list<Order>`) cancellation with stored iterator lookup.
- **Invariant Enforcement**: Validates no crossed book ($\text{Best Bid} < \text{Best Ask}$), strictly positive liquidity ($Q > 0$), and strict price monotonicity on every tick (**12,992 checks, 0 violations** on a 30-minute live capture).
- **Self-Healing State Machine**: Detects desyncs without sequence numbers (via timestamp regression and invariant triggers), transitions through `LIVE` $\rightarrow$ `STALE` $\rightarrow$ `REBUILDING` $\rightarrow$ `LIVE`, and recovers bit-for-bit via snapshot injection across seeded fault injections.
- **Bit-for-Bit Deterministic Replay**: Replays recorded feeds with variable pacing and dual-layer 64-bit FNV-1a state hashing (`0x7bf683492c841f21` verified identical across independent passes).
- **Resilient Gateway & 60fps UI**: Streams NDJSON over TCP (`TCP_NODELAY`), coalesces updates to 50ms per token in Socket.IO, and supports zero-config in-memory fallback when Redis/MongoDB are unavailable.

---

## Empirical Benchmark Results

Measured on Windows 11 (`x86_64`), MinGW-w64 GCC 15.2.0 (`-Wall -Wextra -Wpedantic -O2 -std=c++17`), replaying **12,982 real Polymarket delta updates** pre-loaded into RAM across 5 trials with warmup (`.\build\bench_comparison.exe data/polymarket_recording.jsonl`). Full methodology in [`docs/benchmarks.md`](docs/benchmarks.md).

| Implementation | Mean Throughput | Mean Latency | Median (p50) | p99 Latency | p99.9 Latency |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline `std::map` (RB-Tree)** | 5,474,707 ops/s | 162.32 ns | 100.00 ns | 460.00 ns | 10,440.00 ns |
| **`DirectArrayOrderBook` (Flat Array)** | **7,393,280 ops/s** | **108.02 ns** | **100.00 ns** | **280.00 ns** | **1,720.00 ns** |
| **Speedup / Improvement** | **+35.0% throughput** | **33.5% faster** | — | **39.1% lower** | **6.07x lower tail** |

---

## Repository Layout

```text
/core        C++17 engine (OrderBook, DirectArrayOrderBook, FastMatchingBook, RawRecorder,
             FaultInjector, Replayer, StateHasher, TcpPublisher, LiveEngine, CTest suites)
/gateway     Node.js Express + Socket.IO bridge, Redis StateStore, BullMQ/MongoDB PersistenceWorker
/dashboard   React + Vite + Recharts real-time L2 terminal UI
/data        Sample and golden replay fixtures (large captures gitignored)
/docs        Feed protocol notes (feed-notes.md) & benchmark report (benchmarks.md)
DECISIONS.md Architectural Decision Records (ADR-001 through ADR-012)
Dockerfile   Multi-stage build (C++ builder + Vite builder + Node.js runtime)
render.yaml  Render Blueprint spec for one-click Docker deployment
```

---

## Quick Start (Local)

### Prerequisites
- C++17 compiler (MinGW-w64 GCC / Clang / GCC), CMake $\ge$ 3.15, Ninja, OpenSSL, ZLIB, `nlohmann_json`
- Node.js $\ge$ 20 & npm

### Build & Run Tests
```powershell
# Configure & build C++ targets + React production bundle
cmake -B build -G Ninja
npm run build

# Run C++ unit/property/determinism tests + Gateway tests + Full-Stack E2E test
npm test
```

### Launch Full Stack
```powershell
# Replay recorded 30-minute Polymarket feed at 1x real-time speed and open UI
npm start

# Or connect directly to live Polymarket WebSocket feed:
.\start_all.ps1 -Live
```
Dashboard opens at **`http://localhost:4000`**.

---



## Problems Encountered & Engineering Solutions

| Problem | Root Cause | Solution |
| :--- | :--- | :--- |
| **Polymarket L2 book drift** | `price_change` feed sends *replacement aggregate size*, not cumulative deltas, and omits levels only when `size == "0"`. | Updated [`OrderBook::update_level`](core/src/orderbook.cpp) to overwrite level quantity and erase keys when `size <= 0`. |
| **Release-build test warnings & stripped checks** | `-O2` sets `-DNDEBUG`, stripping standard `assert()` and leaving variables unused. | Replaced `assert()` with an always-active `TEST_ASSERT(cond, msg)` macro across all C++ test suites. |
| **JSONL line splits & Windows file locks** | Initial WebSocket frames contained trailing `\n`; Windows locked open `std::ifstream` handles during test cleanup. | Stripped trailing `\r`/`\n` in [`RawRecorder::write_entry`](core/src/recorder.cpp) and explicitly closed streams before `std::filesystem::remove`. |
| **Empty UI on initial launch (`0 updates`)** | Line-1 initial snapshots were emitted before the Gateway TCP client connected, and dashboard asset IDs mismatched the recorded tokens. | Synchronized all token IDs with [`docs/feed-notes.md`](docs/feed-notes.md) and updated [`live_engine.cpp`](core/src/live_engine.cpp) to broadcast cached snapshots whenever a gateway client joins. |
| **Cross-platform IPC & Linux `SIGPIPE`** | Hardcoded Windows `OPENSSL_ROOT_DIR` and unguarded `send()` calls risked Linux build failures or `SIGPIPE` exits on client disconnect. | Guarded Win32 OpenSSL path in [`CMakeLists.txt`](CMakeLists.txt) and added `MSG_NOSIGNAL` + `SIGPIPE` ignore in [`tcp_publisher.cpp`](core/src/tcp_publisher.cpp). |

---

