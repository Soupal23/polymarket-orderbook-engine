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

// Invariant status report
struct InvariantStatus {
    bool is_valid{true};
    std::string error_detail;
};

// Snapshot of top-N market depth
struct DepthSnapshot {
    std::vector<std::pair<double, double>> bids;
    std::vector<std::pair<double, double>> asks;
};

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

    // Alias for update_level for roadmap clarity
    void apply_delta(Side side, int64_t price_ticks, int64_t size_units) {
        update_level(side, price_ticks, size_units);
    }

    // Invariant Verification
    // Checks:
    // 1. No crossed book (best_bid < best_ask)
    // 2. No non-positive or zero-size levels stored
    // 3. Strict bid descending monotonicity
    // 4. Strict ask ascending monotonicity
    InvariantStatus check_invariants() const;

    // Order-Level Operations & Matching Engine
    std::vector<Trade> add_limit_order(Order order);
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

    // Derived Quantitative Metrics
    std::optional<double> spread() const;
    std::optional<double> mid_price() const;
    std::optional<double> micro_price() const;
    std::optional<double> book_imbalance(size_t depth = 1) const;

    bool is_crossed() const;

    // Depth & Liquidity Inspection
    size_t bid_depth_levels() const { return bids_.size(); }
    size_t ask_depth_levels() const { return asks_.size(); }

    int64_t total_bid_quantity_units() const;
    int64_t total_ask_quantity_units() const;

    DepthSnapshot top_n_depth(size_t depth = 10) const;

    std::vector<std::pair<double, double>> get_bids_l2(size_t depth = 10) const;
    std::vector<std::pair<double, double>> get_asks_l2(size_t depth = 10) const;

    // Reset book state
    void clear();

    // Raw internal access for testing invariants
    void inject_raw_bid_level(int64_t price_ticks, int64_t size_units) {
        bids_[price_ticks].price_ticks = price_ticks;
        bids_[price_ticks].total_quantity_units = size_units;
    }
    void inject_raw_ask_level(int64_t price_ticks, int64_t size_units) {
        asks_[price_ticks].price_ticks = price_ticks;
        asks_[price_ticks].total_quantity_units = size_units;
    }

private:
    std::string asset_id_;
    std::string market_id_;
    std::string last_hash_;
    uint64_t last_timestamp_ms_{0};

    // Bids sorted descending: highest buy price at begin()
    std::map<int64_t, PriceLevel, std::greater<int64_t>> bids_;

    // Asks sorted ascending: lowest sell price at begin()
    std::map<int64_t, PriceLevel, std::less<int64_t>> asks_;

    struct OrderLocation {
        Side side;
        int64_t price_ticks;
    };
    std::unordered_map<uint64_t, OrderLocation> order_locations_;
};

} // namespace polymarket
