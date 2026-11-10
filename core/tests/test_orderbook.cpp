#include "orderbook.hpp"
#include <iostream>
#include <cmath>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED ASSERTION: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;

void test_fixed_point_conversions() {
    std::cout << "[RUNNING] test_fixed_point_conversions..." << std::endl;

    TEST_ASSERT(price_to_ticks(0.52) == 5200, "0.52 to ticks");
    TEST_ASSERT(price_to_ticks(0.01) == 100, "0.01 to ticks");
    TEST_ASSERT(price_to_ticks(0.001) == 10, "0.001 to ticks");
    TEST_ASSERT(price_to_ticks(1.0) == 10000, "1.0 to ticks");
    TEST_ASSERT(price_to_ticks(0.0) == 0, "0.0 to ticks");

    TEST_ASSERT(std::abs(ticks_to_price(5200) - 0.52) < 1e-9, "ticks to 0.52 price");
    TEST_ASSERT(std::abs(ticks_to_price(100) - 0.01) < 1e-9, "ticks to 0.01 price");

    TEST_ASSERT(parse_price_str("0.52") == 5200, "parse 0.52");
    TEST_ASSERT(parse_price_str("0.5200") == 5200, "parse 0.5200");
    TEST_ASSERT(parse_price_str("0.001") == 10, "parse 0.001");
    TEST_ASSERT(parse_price_str("0.4850") == 4850, "parse 0.4850");
    TEST_ASSERT(parse_price_str("1.0") == 10000, "parse 1.0");
    TEST_ASSERT(parse_price_str("0") == 0, "parse 0");

    TEST_ASSERT(parse_qty_str("1500.5") == 15005000, "parse 1500.5");
    TEST_ASSERT(parse_qty_str("100") == 1000000, "parse 100");

    std::cout << "[PASSED] test_fixed_point_conversions" << std::endl;
}

void test_snapshot_and_bbo() {
    std::cout << "[RUNNING] test_snapshot_and_bbo..." << std::endl;

    OrderBook book("asset_123", "market_abc");
    TEST_ASSERT(!book.has_bids(), "empty book bids");
    TEST_ASSERT(!book.has_asks(), "empty book asks");
    TEST_ASSERT(!book.best_bid().has_value(), "no best bid");
    TEST_ASSERT(!book.best_ask().has_value(), "no best ask");
    TEST_ASSERT(!book.spread().has_value(), "no spread");

    std::vector<std::pair<int64_t, int64_t>> bids = {
        {parse_price_str("0.48"), parse_qty_str("1000")},
        {parse_price_str("0.47"), parse_qty_str("2500")},
        {parse_price_str("0.45"), parse_qty_str("5000")}
    };
    std::vector<std::pair<int64_t, int64_t>> asks = {
        {parse_price_str("0.52"), parse_qty_str("800")},
        {parse_price_str("0.55"), parse_qty_str("1200")}
    };

    book.apply_snapshot(bids, asks, "0xhash123", 1700000000000ULL);

    TEST_ASSERT(book.has_bids(), "has bids after snapshot");
    TEST_ASSERT(book.has_asks(), "has asks after snapshot");
    TEST_ASSERT(book.best_bid_ticks().value() == 4800, "best bid ticks 4800");
    TEST_ASSERT(book.best_ask_ticks().value() == 5200, "best ask ticks 5200");
    TEST_ASSERT(std::abs(book.best_bid().value() - 0.48) < 1e-9, "best bid 0.48");
    TEST_ASSERT(std::abs(book.best_ask().value() - 0.52) < 1e-9, "best ask 0.52");
    TEST_ASSERT(std::abs(book.spread().value() - 0.04) < 1e-9, "spread 0.04");
    TEST_ASSERT(std::abs(book.mid_price().value() - 0.50) < 1e-9, "mid price 0.50");

    double expected_micro = (0.48 * 800.0 + 0.52 * 1000.0) / 1800.0;
    TEST_ASSERT(std::abs(book.micro_price().value() - expected_micro) < 1e-6, "micro price");

    TEST_ASSERT(!book.is_crossed(), "book not crossed");

    auto depth = book.top_n_depth(2);
    TEST_ASSERT(depth.bids.size() == 2, "top 2 bids");
    TEST_ASSERT(depth.asks.size() == 2, "top 2 asks");
    TEST_ASSERT(std::abs(depth.bids[0].first - 0.48) < 1e-9, "top bid price 0.48");
    TEST_ASSERT(std::abs(depth.asks[0].first - 0.52) < 1e-9, "top ask price 0.52");

    std::cout << "[PASSED] test_snapshot_and_bbo" << std::endl;
}

void test_incremental_delta_updates() {
    std::cout << "[RUNNING] test_incremental_delta_updates..." << std::endl;

    OrderBook book("asset_123", "market_abc");
    book.apply_delta(Side::BUY, parse_price_str("0.45"), parse_qty_str("100"));
    book.apply_delta(Side::BUY, parse_price_str("0.48"), parse_qty_str("200"));
    book.apply_delta(Side::SELL, parse_price_str("0.55"), parse_qty_str("300"));

    TEST_ASSERT(book.best_bid_ticks().value() == 4800, "best bid 4800");
    TEST_ASSERT(book.best_ask_ticks().value() == 5500, "best ask 5500");

    // Update level size
    book.apply_delta(Side::BUY, parse_price_str("0.48"), parse_qty_str("500"));
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("500"), "updated size 500");

    // Delete best bid (size == 0) -> reverts to 0.45
    book.apply_delta(Side::BUY, parse_price_str("0.48"), 0);
    TEST_ASSERT(book.best_bid_ticks().value() == 4500, "revert best bid to 4500");

    // Delete ask -> empty asks
    book.apply_delta(Side::SELL, parse_price_str("0.55"), 0);
    TEST_ASSERT(!book.has_asks(), "ask book empty");

    std::cout << "[PASSED] test_incremental_delta_updates" << std::endl;
}

void test_book_imbalance() {
    std::cout << "[RUNNING] test_book_imbalance..." << std::endl;

    OrderBook book;
    TEST_ASSERT(!book.book_imbalance().has_value(), "empty book imbalance is nullopt");

    // Equal volume: Imbalance = (100 - 100) / 200 = 0.0
    book.update_level(Side::BUY, parse_price_str("0.49"), parse_qty_str("100"));
    book.update_level(Side::SELL, parse_price_str("0.51"), parse_qty_str("100"));
    TEST_ASSERT(std::abs(book.book_imbalance(1).value() - 0.0) < 1e-9, "neutral imbalance 0.0");

    // More buyers: (300 - 100) / 400 = 200 / 400 = +0.5
    book.update_level(Side::BUY, parse_price_str("0.49"), parse_qty_str("300"));
    TEST_ASSERT(std::abs(book.book_imbalance(1).value() - 0.5) < 1e-9, "positive imbalance +0.5");

    // More sellers: (300 - 900) / 1200 = -600 / 1200 = -0.5
    book.update_level(Side::SELL, parse_price_str("0.51"), parse_qty_str("900"));
    TEST_ASSERT(std::abs(book.book_imbalance(1).value() - (-0.5)) < 1e-9, "negative imbalance -0.5");

    std::cout << "[PASSED] test_book_imbalance" << std::endl;
}

void test_invariants_clean_and_violations() {
    std::cout << "[RUNNING] test_invariants_clean_and_violations..." << std::endl;

    OrderBook book;
    // 1. Clean book passes
    book.update_level(Side::BUY, parse_price_str("0.48"), parse_qty_str("100"));
    book.update_level(Side::BUY, parse_price_str("0.45"), parse_qty_str("200"));
    book.update_level(Side::SELL, parse_price_str("0.52"), parse_qty_str("150"));
    book.update_level(Side::SELL, parse_price_str("0.55"), parse_qty_str("300"));

    auto inv = book.check_invariants();
    TEST_ASSERT(inv.is_valid, "clean book invariants pass");

    // 2. Crossed Book Violation (best bid >= best ask)
    OrderBook crossed_book;
    crossed_book.update_level(Side::BUY, parse_price_str("0.55"), parse_qty_str("100"));
    crossed_book.update_level(Side::SELL, parse_price_str("0.50"), parse_qty_str("100"));
    auto crossed_inv = crossed_book.check_invariants();
    TEST_ASSERT(!crossed_inv.is_valid, "crossed book invariant fails");
    TEST_ASSERT(crossed_inv.error_detail.find("Crossed book") != std::string::npos, "detail mentions crossed");

    // 3. Zero / Negative Quantity Invariant Violation
    OrderBook zero_book;
    zero_book.inject_raw_bid_level(parse_price_str("0.48"), 0); // Raw corrupted level
    auto zero_inv = zero_book.check_invariants();
    TEST_ASSERT(!zero_inv.is_valid, "zero quantity level invariant fails");

    // 4. Empty book is valid
    OrderBook empty_book;
    TEST_ASSERT(empty_book.check_invariants().is_valid, "empty book is valid");

    std::cout << "[PASSED] test_invariants_clean_and_violations" << std::endl;
}

void test_matching_and_cancellation() {
    std::cout << "[RUNNING] test_matching_and_cancellation..." << std::endl;

    OrderBook book;
    book.update_level(Side::SELL, parse_price_str("0.52"), parse_qty_str("100"));
    book.update_level(Side::SELL, parse_price_str("0.54"), parse_qty_str("200"));

    Order buy_order(101, Side::BUY, parse_price_str("0.54"), parse_qty_str("250"));
    auto trades = book.add_limit_order(buy_order);

    TEST_ASSERT(trades.size() == 2, "2 trades generated");
    TEST_ASSERT(trades[0].price_ticks == parse_price_str("0.52"), "trade 0 @ 0.52");
    TEST_ASSERT(trades[1].price_ticks == parse_price_str("0.54"), "trade 1 @ 0.54");
    TEST_ASSERT(book.best_ask_quantity_units().value() == parse_qty_str("50"), "remaining ask 50");

    // Check invariants after match
    TEST_ASSERT(book.check_invariants().is_valid, "invariants pass after match");

    // Order cancellation
    Order resting_buy(201, Side::BUY, parse_price_str("0.45"), parse_qty_str("500"));
    book.add_limit_order(resting_buy);
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("500"), "bid rested");
    bool canceled = book.cancel_order(201);
    TEST_ASSERT(canceled, "order canceled");
    TEST_ASSERT(!book.has_bids(), "bids cleared");
    TEST_ASSERT(book.check_invariants().is_valid, "invariants pass after cancellation");

    std::cout << "[PASSED] test_matching_and_cancellation" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Polymarket OrderBook Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_fixed_point_conversions();
    test_snapshot_and_bbo();
    test_incremental_delta_updates();
    test_book_imbalance();
    test_invariants_clean_and_violations();
    test_matching_and_cancellation();

    std::cout << "========================================" << std::endl;
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
