#include "orderbook.hpp"
#include "fault_injector.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED ASSERTION: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;

struct DeltaUpdate {
    Side side;
    int64_t price_ticks;
    int64_t size_units;
    uint64_t timestamp_ms;
};

// Generate a deterministic synthetic sequence of market updates
std::pair<std::vector<std::pair<int64_t, int64_t>>, std::vector<std::pair<int64_t, int64_t>>>
get_initial_snapshot() {
    std::vector<std::pair<int64_t, int64_t>> bids = {
        {parse_price_str("0.48"), parse_qty_str("1000")},
        {parse_price_str("0.47"), parse_qty_str("2500")},
        {parse_price_str("0.46"), parse_qty_str("5000")},
        {parse_price_str("0.45"), parse_qty_str("8000")}
    };
    std::vector<std::pair<int64_t, int64_t>> asks = {
        {parse_price_str("0.52"), parse_qty_str("1200")},
        {parse_price_str("0.53"), parse_qty_str("3000")},
        {parse_price_str("0.54"), parse_qty_str("6000")},
        {parse_price_str("0.55"), parse_qty_str("9000")}
    };
    return {bids, asks};
}

std::vector<DeltaUpdate> generate_update_stream(size_t count) {
    std::vector<DeltaUpdate> updates;
    updates.reserve(count);

    uint64_t base_ts = 1700000000000ULL;
    for (size_t i = 0; i < count; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        // Bids fluctuate 0.44 - 0.49; Asks fluctuate 0.51 - 0.56
        int64_t price = (side == Side::BUY) 
            ? (4400 + static_cast<int64_t>((i * 7) % 55) * 10)
            : (5100 + static_cast<int64_t>((i * 11) % 55) * 10);

        // Every 6th update removes the level (size = 0)
        int64_t size = (i % 6 == 0) ? 0 : parse_qty_str(std::to_string(100 + (i % 20) * 50));
        uint64_t ts = base_ts + (i + 1) * 50; // Strict 50ms increments

        updates.push_back({side, price, size, ts});
    }
    return updates;
}

void run_property_recovery_test_for_seed(uint64_t seed, bool print_trace = false) {
    auto [init_bids, init_asks] = get_initial_snapshot();

    // 1. Establish Clean Reference Book
    OrderBook clean_book("asset_recovery", "market_recovery");
    clean_book.apply_snapshot(init_bids, init_asks, "hash_0", 1700000000000ULL);
    TEST_ASSERT(clean_book.is_live(), "Clean book is live");

    auto updates = generate_update_stream(200);

    // Apply Phase A (updates 0 to 99) on clean book
    for (size_t i = 0; i < 100; ++i) {
        clean_book.apply_delta(updates[i].side, updates[i].price_ticks, updates[i].size_units, updates[i].timestamp_ms);
    }

    // Capture clean point-in-time recovery snapshot at T=100
    auto snap_bids = clean_book.get_bids_l2(100);
    auto snap_asks = clean_book.get_asks_l2(100);
    std::vector<std::pair<int64_t, int64_t>> recovery_bids;
    std::vector<std::pair<int64_t, int64_t>> recovery_asks;
    for (const auto& [p, q] : snap_bids) recovery_bids.emplace_back(price_to_ticks(p), qty_to_units(q));
    for (const auto& [p, q] : snap_asks) recovery_asks.emplace_back(price_to_ticks(p), qty_to_units(q));
    uint64_t recovery_ts = updates[99].timestamp_ms;

    // Apply Phase B (updates 100 to 199) on clean book
    for (size_t i = 100; i < 200; ++i) {
        clean_book.apply_delta(updates[i].side, updates[i].price_ticks, updates[i].size_units, updates[i].timestamp_ms);
    }
    TEST_ASSERT(clean_book.check_invariants().is_valid, "Clean book invariants valid at end");

    // 2. Establish Test Book under Fault Injection
    OrderBook test_book("asset_recovery", "market_recovery");
    test_book.apply_snapshot(init_bids, init_asks, "hash_0", 1700000000000ULL);

    // Inject real network faults into Phase A updates (0 to 99)
    // We simulate drops, out-of-order deliveries, and timestamp regressions
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    bool desync_triggered = false;
    std::string detected_reason;

    if (print_trace) {
        std::cout << "\n--- [TRACE START: Seed " << seed << "] ---" << std::endl;
        std::cout << "Clean Book Initial BBO: Bid=" << clean_book.best_bid().value_or(0)
                  << ", Ask=" << clean_book.best_ask().value_or(0) << std::endl;
    }

    // Corrupt Phase A by dropping a crucial ask level and injecting a high out-of-order bid
    for (size_t i = 0; i < 100; ++i) {
        double r = dist(rng);
        if (r < 0.15) {
            // Drop packet
            continue;
        } else if (r < 0.30) {
            // Out of order: inject inverted price that causes crossed book or timestamp regression
            uint64_t regressed_ts = updates[i].timestamp_ms > 200 ? updates[i].timestamp_ms - 200 : 0;
            bool ok = test_book.apply_delta(updates[i].side, updates[i].price_ticks, updates[i].size_units, regressed_ts);
            if (!ok && !desync_triggered) {
                desync_triggered = true;
                detected_reason = test_book.get_stale_reason();
            }
        } else {
            bool ok = test_book.apply_delta(updates[i].side, updates[i].price_ticks, updates[i].size_units, updates[i].timestamp_ms);
            if (!ok && !desync_triggered) {
                desync_triggered = true;
                detected_reason = test_book.get_stale_reason();
            }
        }
    }

    // If stochastic corruption did not hit, inject an explicit crossed order to guarantee desync trigger
    if (!desync_triggered) {
        test_book.apply_delta(Side::BUY, parse_price_str("0.58"), parse_qty_str("500"), updates[99].timestamp_ms + 1);
        desync_triggered = test_book.is_stale();
        detected_reason = test_book.get_stale_reason();
    }

    TEST_ASSERT(desync_triggered, "Desync must be detected");
    TEST_ASSERT(test_book.is_stale(), "Test book must be in STALE state");

    if (print_trace) {
        std::cout << "[FAULT DETECTED]: Book marked STALE." << std::endl;
        std::cout << "  Reason: " << detected_reason << std::endl;
        std::cout << "  Test Book State: " << to_string(test_book.get_state()) << std::endl;
        std::cout << "  Desynced Test Book != Clean Book: " << (test_book != clean_book ? "TRUE (Diverged)" : "FALSE") << std::endl;
    }

    // 3. Self-Healing: Trigger Snapshot Recovery
    test_book.start_rebuild();
    TEST_ASSERT(test_book.is_rebuilding(), "Book entered REBUILDING state");
    if (print_trace) {
        std::cout << "[SELF-HEALING]: Transitioned to REBUILDING. Applying fresh snapshot..." << std::endl;
    }

    test_book.recover_with_snapshot(recovery_bids, recovery_asks, "hash_recovery_100", recovery_ts);
    TEST_ASSERT(test_book.is_live(), "Book returned to LIVE state after snapshot recovery");
    if (print_trace) {
        std::cout << "[RECOVERY COMPLETE]: Book returned to LIVE state. Resuming stream..." << std::endl;
    }

    // 4. Resume stream with Phase B (updates 100 to 199)
    for (size_t i = 100; i < 200; ++i) {
        test_book.apply_delta(updates[i].side, updates[i].price_ticks, updates[i].size_units, updates[i].timestamp_ms);
    }

    // 5. PROPERTY ASSERTION: The recovered book matches the clean book bit-for-bit!
    TEST_ASSERT(test_book == clean_book, "Recovered book matches clean reference book exactly!");
    TEST_ASSERT(test_book.best_bid() == clean_book.best_bid(), "Best bid matches!");
    TEST_ASSERT(test_book.best_ask() == clean_book.best_ask(), "Best ask matches!");
    TEST_ASSERT(test_book.spread() == clean_book.spread(), "Spread matches!");
    TEST_ASSERT(std::abs(test_book.micro_price().value() - clean_book.micro_price().value()) < 1e-9, "Micro-price matches!");
    TEST_ASSERT(test_book.bid_depth_levels() == clean_book.bid_depth_levels(), "Bid depth count matches!");
    TEST_ASSERT(test_book.ask_depth_levels() == clean_book.ask_depth_levels(), "Ask depth count matches!");

    if (print_trace) {
        std::cout << "[VERIFIED]: Property test passed! Test book == Clean book bit-for-bit." << std::endl;
        std::cout << "  Final BBO: Bid=" << test_book.best_bid().value() << ", Ask=" << test_book.best_ask().value() << std::endl;
        std::cout << "  Final Spread: " << test_book.spread().value() << " | Micro: " << test_book.micro_price().value() << std::endl;
        std::cout << "--- [TRACE END] ---\n" << std::endl;
    }
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "Starting Phase 3 Fault Injection & Self-Healing Test Suite" << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::vector<uint64_t> seeds = {42, 101, 777, 1337, 99999};

    for (size_t i = 0; i < seeds.size(); ++i) {
        std::cout << "[RUNNING] Property Recovery Test with Seed " << seeds[i] << "..." << std::endl;
        // Print detailed trace for the first seed
        run_property_recovery_test_for_seed(seeds[i], (i == 0));
        std::cout << "[PASSED] Seed " << seeds[i] << " successfully recovered." << std::endl;
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "ALL FAULT INJECTION & SELF-HEALING TESTS PASSED!" << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
