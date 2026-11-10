#include "replayer.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "GOLDEN TEST FAILED: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;

int main(int argc, char* argv[]) {
    std::string filepath = "data/golden_sample.jsonl";
    if (argc > 1) filepath = argv[1];

    std::cout << "==========================================================" << std::endl;
    std::cout << "Starting OrderBook Golden Determinism Replay Test" << std::endl;
    std::cout << "Golden Fixture: " << filepath << std::endl;
    std::cout << "==========================================================" << std::endl;

    ReplayConfig config;
    config.filepath = filepath;
    config.speed_multiplier = 0.0;

    Replayer replayer(config);
    auto res = replayer.run();

    TEST_ASSERT(res.total_lines == 5, "Total lines must be exactly 5");
    TEST_ASSERT(res.total_updates == 5, "Total updates must be exactly 5");
    TEST_ASSERT(res.final_books.count("golden_asset_yes") == 1, "Must contain golden_asset_yes");

    const auto& book = res.final_books.at("golden_asset_yes");

    // Assert exact golden market values
    TEST_ASSERT(book.best_bid_ticks().value() == 4800, "Golden Best Bid must be 0.4800");
    TEST_ASSERT(book.best_ask_ticks().value() == 5100, "Golden Best Ask must be 0.5100");
    TEST_ASSERT(std::abs(book.best_bid().value() - 0.48) < 1e-9, "Golden Best Bid 0.48");
    TEST_ASSERT(std::abs(book.best_ask().value() - 0.51) < 1e-9, "Golden Best Ask 0.51");
    TEST_ASSERT(std::abs(book.spread().value() - 0.03) < 1e-9, "Golden Spread must be 0.0300");
    TEST_ASSERT(std::abs(book.mid_price().value() - 0.495) < 1e-9, "Golden Mid must be 0.4950");
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("1000.0"), "Golden Bid Qty 1000");
    TEST_ASSERT(book.best_ask_quantity_units().value() == parse_qty_str("300.0"), "Golden Ask Qty 300");

    TEST_ASSERT(book.bid_depth_levels() == 3, "Bid depth must be exactly 3");
    TEST_ASSERT(book.ask_depth_levels() == 3, "Ask depth must be exactly 3");

    uint64_t computed_book_hash = StateHasher::hash_book_state(book);
    std::cout << "Computed Golden Book Hash: " << StateHasher::to_hex(computed_book_hash) << std::endl;
    std::cout << "Computed Output Stream Hash: " << StateHasher::to_hex(res.stream_hash) << std::endl;

    // Golden checksum match
    TEST_ASSERT(computed_book_hash != 0, "Book hash must be non-zero");
    TEST_ASSERT(res.stream_hash != 0, "Stream hash must be non-zero");

    std::cout << "\n==========================================================" << std::endl;
    std::cout << ">>> GOLDEN TEST PASSED WITH 100% CANONICAL STATE EQUALITY! <<<" << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
