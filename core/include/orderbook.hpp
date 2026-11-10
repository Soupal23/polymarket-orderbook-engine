#pragma once

#include "types.hpp"
#include "order.hpp"
#include "price_level.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>

namespace polymarket {

class OrderBook {
public:
    explicit OrderBook(std::string asset_id = "", std::string market_id = "");

    // Metadata
    const std::string& get_asset_id() const { return asset_id_; }
    const std::string& get_market_id() const { return market_id_; }
    const std::string& get_last_hash() const { return last_hash_; }
    uint64_t get_last_timestamp_ms() const { return last_timestamp_ms_; }

    void set_metadata(const std::string& hash, uint64_t timestamp_ms) {
        last_hash_ = hash;
        last_timestamp_ms_ = timestamp_ms;
    }

    // L2 Feed Operations (Directly mirrors Polymarket CLOB book snapshot & price_change)
    void apply_snapshot(
        const std::vector<std::pair<int64_t, int64_t>>& bids_ticks_qty,
        const std::vector<std::pair<int64_t, int64_t>>& asks_ticks_qty,
        const std::string& hash = "",
        uint64_t timestamp_ms = 0
    );

    // Incremental level update (Mirrors Polymarket 'price_change' event)
    // If size_units == 0, the level is deleted from the book.
    void update_level(Side side, int64_t price_ticks, int64_t size_units);

    // Order-Level Operations & Matching Engine
    // Matches incoming aggressive limit order against opposite resting book.
    // Generates execution trades and rests any unfilled remainder in the book.
    std::vector<Trade> add_limit_order(Order order);

    // Cancel order by ID (if placed via add_limit_order)
    bool cancel_order(uint64_t order_id);

    // Market State & Top-of-Book (BBO) Queries
    bool has_bids() const { return !bids_.empty(); }
    bool has_asks() const { return !asks_.empty(); }

    std::optional<int64_t> best_bid_ticks() const;
    std::optional<int64_t> best_ask_ticks() const;

    std::optional<double> best_bid() const;
    std::optional<double> best_ask() const;

    std::optional<int64_t> best_bid_quantity_units() const;
    std::optional<int64_t> best_ask_quantity_units() const;

    std::optional<double> spread() const;
    std::optional<double> mid_price() const;
    std::optional<double> micro_price() const;

    bool is_crossed() const;

    // Depth & Liquidity Inspection
    size_t bid_depth_levels() const { return bids_.size(); }
    size_t ask_depth_levels() const { return asks_.size(); }

    int64_t total_bid_quantity_units() const;
    int64_t total_ask_quantity_units() const;

    // Extract top N price levels as (price, quantity) in human-readable doubles
    std::vector<std::pair<double, double>> get_bids_l2(size_t depth = 10) const;
    std::vector<std::pair<double, double>> get_asks_l2(size_t depth = 10) const;

    // Reset book state
    void clear();

private:
    std::string asset_id_;
    std::string market_id_;
    std::string last_hash_;
    uint64_t last_timestamp_ms_{0};

    // Bids sorted descending: highest buy price at begin()
    std::map<int64_t, PriceLevel, std::greater<int64_t>> bids_;

    // Asks sorted ascending: lowest sell price at begin()
    std::map<int64_t, PriceLevel, std::less<int64_t>> asks_;

    // Map order_id -> (side, price_ticks) for O(1) order lookup during cancellation
    struct OrderLocation {
        Side side;
        int64_t price_ticks;
    };
    std::unordered_map<uint64_t, OrderLocation> order_locations_;
};

} // namespace polymarket
