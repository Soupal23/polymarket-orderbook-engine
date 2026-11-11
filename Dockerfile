# ==============================================================================
# Stage 1: Build & Test C++17 Orderbook Core Engine
# ==============================================================================
FROM debian:bookworm-slim AS cpp-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libssl-dev \
    zlib1g-dev \
    nlohmann-json3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY core/ ./core/
COPY data/sample.jsonl data/golden_sample.jsonl ./data/

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target live_engine test_orderbook test_recorder replay_invariants test_fault_recovery test_equivalence test_golden_replay test_determinism test_tcp_publisher \
    && ctest --test-dir build --output-on-failure

# ==============================================================================
# Stage 2: Build React + Vite Dashboard
# ==============================================================================
FROM node:22-bookworm-slim AS dashboard-builder

WORKDIR /app/dashboard
COPY dashboard/package*.json ./
RUN npm ci
COPY dashboard/ ./
RUN npm run build

# ==============================================================================
# Stage 3: Production Runtime (C++ Engine + Node.js Gateway + Static UI)
# ==============================================================================
FROM node:22-bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    zlib1g \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Install Gateway production dependencies
COPY gateway/package*.json ./gateway/
RUN cd gateway && npm ci --omit=dev

# Copy Gateway source, Dashboard dist, compiled C++ binary, and sample logs
COPY gateway/ ./gateway/
COPY --from=dashboard-builder /app/dashboard/dist ./dashboard/dist
COPY --from=cpp-builder /src/build/live_engine ./build/live_engine
COPY data/golden_sample.jsonl data/sample.jsonl ./data/
COPY docker-entrypoint.mjs ./

ENV NODE_ENV=production
ENV PORT=10000
ENV CORE_TCP_PORT=9001
ENV ENGINE_MODE=live

EXPOSE 10000

CMD ["node", "docker-entrypoint.mjs"]
