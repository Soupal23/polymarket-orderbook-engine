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

    // Direct string parser to prevent float drift
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

    // Micro-price calculation:
    // (0.48 * 800 + 0.52 * 1000) / (1000 + 800) = (384 + 520) / 1800 = 904 / 1800 = 0.502222...
    double expected_micro = (0.48 * 800.0 + 0.52 * 1000.0) / 1800.0;
    TEST_ASSERT(std::abs(book.micro_price().value() - expected_micro) < 1e-6, "micro price");

    TEST_ASSERT(!book.is_crossed(), "book not crossed");

    auto l2_bids = book.get_bids_l2(2);
    TEST_ASSERT(l2_bids.size() == 2, "l2 bids size");
    TEST_ASSERT(std::abs(l2_bids[0].first - 0.48) < 1e-9, "l2 bid 0 price");
    TEST_ASSERT(std::abs(l2_bids[0].second - 1000.0) < 1e-9, "l2 bid 0 qty");
    TEST_ASSERT(std::abs(l2_bids[1].first - 0.47) < 1e-9, "l2 bid 1 price");

    std::cout << "[PASSED] test_snapshot_and_bbo" << std::endl;
}

void test_incremental_price_change() {
    std::cout << "[RUNNING] test_incremental_price_change..." << std::endl;

    OrderBook book("asset_123", "market_abc");
    book.update_level(Side::BUY, parse_price_str("0.45"), parse_qty_str("100"));
    book.update_level(Side::BUY, parse_price_str("0.48"), parse_qty_str("200"));
    book.update_level(Side::SELL, parse_price_str("0.55"), parse_qty_str("300"));

    TEST_ASSERT(book.best_bid_ticks().value() == 4800, "best bid 4800");
    TEST_ASSERT(book.best_ask_ticks().value() == 5500, "best ask 5500");

    // Update existing level size
    book.update_level(Side::BUY, parse_price_str("0.48"), parse_qty_str("500"));
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("500"), "updated size 500");

    // Higher bid arrives -> new best bid
    book.update_level(Side::BUY, parse_price_str("0.50"), parse_qty_str("150"));
    TEST_ASSERT(book.best_bid_ticks().value() == 5000, "new best bid 5000");

    // Delete best bid (size == 0) -> reverts to 0.48
    book.update_level(Side::BUY, parse_price_str("0.50"), 0);
    TEST_ASSERT(book.best_bid_ticks().value() == 4800, "revert best bid to 4800");

    // Delete ask -> ask book becomes empty
    book.update_level(Side::SELL, parse_price_str("0.55"), 0);
    TEST_ASSERT(!book.has_asks(), "ask book empty");
    TEST_ASSERT(!book.best_ask().has_value(), "no best ask");

    std::cout << "[PASSED] test_incremental_price_change" << std::endl;
}

void test_matching_partial_and_multi_level() {
    std::cout << "[RUNNING] test_matching_partial_and_multi_level..." << std::endl;

    OrderBook book;
    // Set up ask liquidity across 2 levels:
    // Level 1: 0.52 with 100 units
    // Level 2: 0.54 with 200 units
    book.update_level(Side::SELL, parse_price_str("0.52"), parse_qty_str("100"));
    book.update_level(Side::SELL, parse_price_str("0.54"), parse_qty_str("200"));

    // Incoming aggressive BUY order for 250 units with limit 0.54
    // Should match: 100 @ 0.52, then 150 @ 0.54. Remaining at 0.54 level should be 50 units.
    Order aggressive_buy(101, Side::BUY, parse_price_str("0.54"), parse_qty_str("250"), 1000);
    auto trades = book.add_limit_order(aggressive_buy);

    TEST_ASSERT(trades.size() == 2, "trades size == 2");
    TEST_ASSERT(trades[0].price_ticks == parse_price_str("0.52"), "trade 0 price 0.52");
    TEST_ASSERT(trades[0].quantity_units == parse_qty_str("100"), "trade 0 qty 100");
    TEST_ASSERT(trades[0].taker_side == Side::BUY, "trade 0 side BUY");

    TEST_ASSERT(trades[1].price_ticks == parse_price_str("0.54"), "trade 1 price 0.54");
    TEST_ASSERT(trades[1].quantity_units == parse_qty_str("150"), "trade 1 qty 150");

    // Best ask should now be 0.54 with remaining 50 units
    TEST_ASSERT(book.best_ask_ticks().value() == parse_price_str("0.54"), "best ask 0.54");
    TEST_ASSERT(book.best_ask_quantity_units().value() == parse_qty_str("50"), "remaining ask qty 50");
    TEST_ASSERT(!book.has_bids(), "no bids rested");

    std::cout << "[PASSED] test_matching_partial_and_multi_level" << std::endl;
}

void test_crossing_with_resting_remainder() {
    std::cout << "[RUNNING] test_crossing_with_resting_remainder..." << std::endl;

    OrderBook book;
    book.update_level(Side::SELL, parse_price_str("0.52"), parse_qty_str("100"));

    // Incoming aggressive BUY for 150 units with limit 0.55
    // Consumes 100 @ 0.52, and the remaining 50 units should rest on the BUY book at 0.55!
    Order aggressive_buy(201, Side::BUY, parse_price_str("0.55"), parse_qty_str("150"), 2000);
    auto trades = book.add_limit_order(aggressive_buy);

    TEST_ASSERT(trades.size() == 1, "trades size == 1");
    TEST_ASSERT(trades[0].price_ticks == parse_price_str("0.52"), "trade 0 price 0.52");
    TEST_ASSERT(trades[0].quantity_units == parse_qty_str("100"), "trade 0 qty 100");

    // Ask book is now depleted
    TEST_ASSERT(!book.has_asks(), "ask book empty");

    // Bid book now has the resting 50 units at 0.55
    TEST_ASSERT(book.has_bids(), "bids has resting remainder");
    TEST_ASSERT(book.best_bid_ticks().value() == parse_price_str("0.55"), "best bid 0.55");
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("50"), "resting qty 50");

    std::cout << "[PASSED] test_crossing_with_resting_remainder" << std::endl;
}

void test_order_cancellation() {
    std::cout << "[RUNNING] test_order_cancellation..." << std::endl;

    OrderBook book;
    Order buy1(301, Side::BUY, parse_price_str("0.45"), parse_qty_str("100"));
    Order buy2(302, Side::BUY, parse_price_str("0.45"), parse_qty_str("200"));
    book.add_limit_order(buy1);
    book.add_limit_order(buy2);

    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("300"), "total bid qty 300");

    // Cancel first order
    bool ok1 = book.cancel_order(301);
    TEST_ASSERT(ok1, "cancel order 301");
    TEST_ASSERT(book.best_bid_quantity_units().value() == parse_qty_str("200"), "remaining bid qty 200");

    // Cancel second order
    bool ok2 = book.cancel_order(302);
    TEST_ASSERT(ok2, "cancel order 302");
    TEST_ASSERT(!book.has_bids(), "all bids canceled");

    // Cancel non-existent order
    bool ok3 = book.cancel_order(999);
    TEST_ASSERT(!ok3, "cancel non-existent order");

    std::cout << "[PASSED] test_order_cancellation" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Polymarket OrderBook Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_fixed_point_conversions();
    test_snapshot_and_bbo();
    test_incremental_price_change();
    test_matching_partial_and_multi_level();
    test_crossing_with_resting_remainder();
    test_order_cancellation();

    std::cout << "========================================" << std::endl;
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
