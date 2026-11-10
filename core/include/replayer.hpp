#pragma once

#include "orderbook.hpp"
#include "state_hasher.hpp"
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace polymarket {

struct ReplayConfig {
    std::string filepath = "data/polymarket_recording.jsonl";
    double speed_multiplier = 0.0; // 0.0 = as fast as possible (unthrottled), 1.0 = real-time, 10.0 = 10x
    std::string filter_asset_id = "";
};

struct ReplayResult {
    uint64_t total_lines{0};
    uint64_t total_updates{0};
    double elapsed_wall_seconds{0.0};
    double updates_per_sec{0.0};
    uint64_t stream_hash{StateHasher::FNV_OFFSET_BASIS};
    std::unordered_map<std::string, uint64_t> final_book_hashes;
    std::unordered_map<std::string, OrderBook> final_books;
};

class Replayer {
public:
    explicit Replayer(ReplayConfig config);

    // Runs deterministic replay and returns hashes and execution metrics
    ReplayResult run(std::function<void(const std::string& asset_id, const OrderBook& book)> on_update = nullptr);

private:
    ReplayConfig config_;
};

} // namespace polymarket
