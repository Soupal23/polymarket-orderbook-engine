#include "orderbook.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>

using namespace polymarket;

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "Polymarket OrderBook Benchmark Runner" << std::endl;
#if defined(__clang__)
    std::cout << "Compiler: Clang " << __clang_version__ << std::endl;
#elif defined(__GNUC__)
    std::cout << "Compiler: GCC " << __VERSION__ << std::endl;
#elif defined(_MSC_VER)
    std::cout << "Compiler: MSVC " << _MSC_VER << std::endl;
#else
    std::cout << "Compiler: Standard C++17" << std::endl;
#endif
#if defined(__OPTIMIZE__) || defined(NDEBUG)
    std::cout << "Optimization: Enabled (-O3)" << std::endl;
#else
    std::cout << "Optimization: None / Debug" << std::endl;
#endif
    std::cout << "==========================================================" << std::endl;

    constexpr int NUM_ITERATIONS = 500000;
    OrderBook book("asset_bench", "market_bench");

    // Benchmark 1: Level Updates (Simulating Polymarket WebSocket price_change feed)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            // Price oscillates between 0.40 and 0.60
            int64_t price = 4000 + (i % 200) * 10;
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            int64_t qty = (i % 5 == 0) ? 0 : (100 + (i % 50)) * QTY_SCALE;
            book.update_level(side, price, qty);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double ops_per_sec = NUM_ITERATIONS / diff.count();
        double avg_ns_per_op = (diff.count() * 1e9) / NUM_ITERATIONS;

        std::cout << "[BENCH 1] L2 Level Updates (price_change feed):" << std::endl;
        std::cout << "  Iterations: " << NUM_ITERATIONS << std::endl;
        std::cout << "  Elapsed Time: " << std::fixed << std::setprecision(4) << diff.count() << " seconds" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec" << std::endl;
        std::cout << "  Mean Latency: " << std::fixed << std::setprecision(2) << avg_ns_per_op << " ns/op" << std::endl;
    }

    // Benchmark 2: BBO Queries (Top-of-book retrieval)
    {
        auto start = std::chrono::high_resolution_clock::now();
        volatile double dummy = 0.0;
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            auto bb = book.best_bid();
            auto ba = book.best_ask();
            if (bb && ba) {
                dummy += *bb + *ba;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double ops_per_sec = NUM_ITERATIONS / diff.count();
        double avg_ns_per_op = (diff.count() * 1e9) / NUM_ITERATIONS;

        std::cout << "\n[BENCH 2] BBO Top-of-Book Queries:" << std::endl;
        std::cout << "  Iterations: " << NUM_ITERATIONS << std::endl;
        std::cout << "  Elapsed Time: " << std::fixed << std::setprecision(4) << diff.count() << " seconds" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec" << std::endl;
        std::cout << "  Mean Latency: " << std::fixed << std::setprecision(2) << avg_ns_per_op << " ns/op" << std::endl;
    }

    // Benchmark 3: Order Matching & Crossing Engine
    {
        book.clear();
        // Seed 100 ask levels
        for (int i = 50; i < 150; ++i) {
            book.update_level(Side::SELL, i * 100, 1000 * QTY_SCALE);
        }

        constexpr int MATCH_ITERATIONS = 200000;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < MATCH_ITERATIONS; ++i) {
            // Aggressive buy matching against asks
            Order aggressive_buy(i + 1, Side::BUY, 6000, 1 * QTY_SCALE, 0);
            book.add_limit_order(aggressive_buy);

            // Periodically replenish ask liquidity
            if (i % 1000 == 0) {
                book.update_level(Side::SELL, 5500, 5000 * QTY_SCALE);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        double ops_per_sec = MATCH_ITERATIONS / diff.count();
        double avg_ns_per_op = (diff.count() * 1e9) / MATCH_ITERATIONS;

        std::cout << "\n[BENCH 3] Order Matching & Crossing:" << std::endl;
        std::cout << "  Iterations: " << MATCH_ITERATIONS << std::endl;
        std::cout << "  Elapsed Time: " << std::fixed << std::setprecision(4) << diff.count() << " seconds" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec" << std::endl;
        std::cout << "  Mean Latency: " << std::fixed << std::setprecision(2) << avg_ns_per_op << " ns/op" << std::endl;
    }

    std::cout << "==========================================================" << std::endl;
    return 0;
}
