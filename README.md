# Mini Real-Time Orderbook Engine for Prediction Markets

A high-performance orderbook engine and market data pipeline built for Polymarket prediction markets.

## Repository Layout

```text
/core        C++17 orderbook engine, recorder, replayer, benchmarks, tests
/gateway     Node service: socket from core -> Redis -> Socket.IO, BullMQ persistence
/dashboard   React + Vite + Recharts
/data        recorded feeds (gitignored except a small sample)
/docs        architecture diagram, benchmark results, feed notes
README.md, DECISIONS.md
```

## Tech Stack

- **Core Engine**: C++17, CMake (`-O2`, `-Wall -Wextra -Wpedantic`)
- **Gateway**: Node.js, Express, Socket.IO, Redis, BullMQ, MongoDB
- **Dashboard**: React, Vite, Recharts

## Documentation

- [`docs/feed-notes.md`](docs/feed-notes.md): Polymarket CLOB WebSocket & Gamma REST protocol specifications, payload schemas, and target markets.
- [`DECISIONS.md`](DECISIONS.md): Architectural decisions log (ADRs) with rationale and evaluated alternatives.
