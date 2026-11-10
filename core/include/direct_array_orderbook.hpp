#pragma once

#include "types.hpp"
#include "orderbook.hpp"
#include <array>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>

namespace polymarket {

// Total ticks for bounded prediction market price domain [0.0000, 1.0000] with scale 10,000
constexpr size_t MAX_TICKS = 10001;

class DirectArrayOrderBook {
public:
    explicit DirectArrayOrderBook(std::string asset_id = "", std::string market_id = "");

    void clear();

    // Snapshot ingestion
    void apply_snapshot(
        const std::vector<std::pair<int64_t, int64_t>>& bids_ticks_qty,
        const std::vector<std::pair<int64_t, int64_t>>& asks_ticks_qty,
        const std::string& hash = "",
        uint64_t timestamp_ms = 0
    );

    // O(1) Level Update directly indexing the flat array
    bool update_level(Side side, int64_t price_ticks, int64_t size_units, uint64_t timestamp_ms = 0);

    bool apply_delta(Side side, int64_t price_ticks, int64_t size_units, uint64_t timestamp_ms = 0) {
        return update_level(side, price_ticks, size_units, timestamp_ms);
    }

    // Top-of-Book queries
    bool has_bids() const { return best_bid_tick_ >= 0; }
    bool has_asks() const { return best_ask_tick_ < static_cast<int64_t>(MAX_TICKS); }

    std::optional<int64_t> best_bid_ticks() const;
    std::optional<int64_t> best_ask_ticks() const;

    std::optional<double> best_bid() const;
    std::optional<double> best_ask() const;

    std::optional<int64_t> best_bid_quantity_units() const;
    std::optional<int64_t> best_ask_quantity_units() const;

    std::optional<double> spread() const;
    std::optional<double> mid_price() const;
    std::optional<double> micro_price() const;
    std::optional<double> book_imbalance(size_t depth = 1) const;

    bool is_crossed() const;
    InvariantStatus check_invariants() const;

    std::vector<std::pair<double, double>> get_bids_l2(size_t depth = 10) const;
    std::vector<std::pair<double, double>> get_asks_l2(size_t depth = 10) const;

    size_t bid_depth_levels() const { return active_bid_count_; }
    size_t ask_depth_levels() const { return active_ask_count_; }

    const std::string& get_asset_id() const { return asset_id_; }
    const std::string& get_market_id() const { return market_id_; }

private:
    void recompute_best_bid();
    void recompute_best_ask();

    std::string asset_id_;
    std::string market_id_;
    std::string last_hash_;
    uint64_t last_timestamp_ms_{0};

    // Flat, cache-contiguous arrays for direct O(1) indexing by tick
    std::array<int64_t, MAX_TICKS> bid_levels_{};
    std::array<int64_t, MAX_TICKS> ask_levels_{};

    int64_t best_bid_tick_{-1};
    int64_t best_ask_tick_{static_cast<int64_t>(MAX_TICKS)};

    size_t active_bid_count_{0};
    size_t active_ask_count_{0};
};

} // namespace polymarket
