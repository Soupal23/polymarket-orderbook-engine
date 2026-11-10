#include "replayer.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace polymarket {

Replayer::Replayer(ReplayConfig config)
    : config_(std::move(config)) {}

ReplayResult Replayer::run(std::function<void(const std::string& asset_id, const OrderBook& book)> on_update) {
    ReplayResult result;
    std::ifstream file(config_.filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Replayer unable to open file: " << config_.filepath << std::endl;
        return result;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t prev_recv_ns = 0;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        result.total_lines++;

        try {
            json j = json::parse(line);
            if (!j.contains("raw")) continue;

            uint64_t current_recv_ns = j.value("recv_time_ns", 0ULL);

            // Optional Real-time or Paced Replay
            if (config_.speed_multiplier > 0.0 && prev_recv_ns > 0 && current_recv_ns > prev_recv_ns) {
                int64_t delta_ns = current_recv_ns - prev_recv_ns;
                // Cap sleep at 500ms to skip prolonged idle gaps
                delta_ns = std::min<int64_t>(delta_ns, 500000000LL);
                int64_t sleep_ns = static_cast<int64_t>(delta_ns / config_.speed_multiplier);
                if (sleep_ns > 1000) { // Only sleep if > 1 microsecond
                    std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
                }
            }
            prev_recv_ns = current_recv_ns;

            auto& raw = j["raw"];

            // 1. Initial Snapshot Array
            if (raw.is_array()) {
                for (auto& item : raw) {
                    if (!item.contains("asset_id")) continue;
                    std::string asset_id = item["asset_id"].get<std::string>();
                    if (!config_.filter_asset_id.empty() && asset_id != config_.filter_asset_id) continue;

                    std::string market = item.value("market", "");
                    std::string hash = item.value("hash", "");
                    uint64_t ts = 0;
                    if (item.contains("timestamp") && item["timestamp"].is_string()) {
                        ts = std::stoull(item["timestamp"].get<std::string>());
                    }

                    std::vector<std::pair<int64_t, int64_t>> bids;
                    if (item.contains("bids") && item["bids"].is_array()) {
                        for (auto& b : item["bids"]) {
                            bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                              parse_qty_str(b["size"].get<std::string>()));
                        }
                    }

                    std::vector<std::pair<int64_t, int64_t>> asks;
                    if (item.contains("asks") && item["asks"].is_array()) {
                        for (auto& a : item["asks"]) {
                            asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                              parse_qty_str(a["size"].get<std::string>()));
                        }
                    }

                    if (result.final_books.find(asset_id) == result.final_books.end()) {
                        result.final_books.emplace(asset_id, OrderBook(asset_id, market));
                    }

                    auto& book = result.final_books[asset_id];
                    book.apply_snapshot(bids, asks, hash, ts);
                    result.total_updates++;

                    // Update rolling output stream hash
                    StateHasher::update_stream_hash(
                        result.stream_hash,
                        book.best_bid_ticks().value_or(0),
                        book.best_ask_ticks().value_or(0),
                        book.best_bid_quantity_units().value_or(0),
                        book.best_ask_quantity_units().value_or(0)
                    );

                    if (on_update) on_update(asset_id, book);
                }
            }
            // 2. Single Object Event
            else if (raw.is_object()) {
                std::string event_type = raw.value("event_type", "");

                if (event_type == "book") {
                    if (!raw.contains("asset_id")) continue;
                    std::string asset_id = raw["asset_id"].get<std::string>();
                    if (!config_.filter_asset_id.empty() && asset_id != config_.filter_asset_id) continue;

                    std::string market = raw.value("market", "");
                    std::string hash = raw.value("hash", "");
                    uint64_t ts = 0;
                    if (raw.contains("timestamp") && raw["timestamp"].is_string()) {
                        ts = std::stoull(raw["timestamp"].get<std::string>());
                    }

                    std::vector<std::pair<int64_t, int64_t>> bids;
                    if (raw.contains("bids") && raw["bids"].is_array()) {
                        for (auto& b : raw["bids"]) {
                            bids.emplace_back(parse_price_str(b["price"].get<std::string>()),
                                              parse_qty_str(b["size"].get<std::string>()));
                        }
                    }

                    std::vector<std::pair<int64_t, int64_t>> asks;
                    if (raw.contains("asks") && raw["asks"].is_array()) {
                        for (auto& a : raw["asks"]) {
                            asks.emplace_back(parse_price_str(a["price"].get<std::string>()),
                                              parse_qty_str(a["size"].get<std::string>()));
                        }
                    }

                    if (result.final_books.find(asset_id) == result.final_books.end()) {
                        result.final_books.emplace(asset_id, OrderBook(asset_id, market));
                    }

                    auto& book = result.final_books[asset_id];
                    book.apply_snapshot(bids, asks, hash, ts);
                    result.total_updates++;

                    StateHasher::update_stream_hash(
                        result.stream_hash,
                        book.best_bid_ticks().value_or(0),
                        book.best_ask_ticks().value_or(0),
                        book.best_bid_quantity_units().value_or(0),
                        book.best_ask_quantity_units().value_or(0)
                    );

                    if (on_update) on_update(asset_id, book);
                } else if (event_type == "price_change") {
                    if (raw.contains("price_changes") && raw["price_changes"].is_array()) {
                        for (auto& change : raw["price_changes"]) {
                            if (!change.contains("asset_id")) continue;
                            std::string asset_id = change["asset_id"].get<std::string>();
                            if (!config_.filter_asset_id.empty() && asset_id != config_.filter_asset_id) continue;

                            std::string price_str = change["price"].get<std::string>();
                            std::string size_str = change["size"].get<std::string>();
                            std::string side_str = change["side"].get<std::string>();

                            Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                            int64_t price_ticks = parse_price_str(price_str);
                            int64_t size_units = parse_qty_str(size_str);

                            if (result.final_books.find(asset_id) == result.final_books.end()) {
                                result.final_books.emplace(asset_id, OrderBook(asset_id, raw.value("market", "")));
                            }

                            auto& book = result.final_books[asset_id];
                            book.apply_delta(side, price_ticks, size_units);
                            result.total_updates++;

                            StateHasher::update_stream_hash(
                                result.stream_hash,
                                book.best_bid_ticks().value_or(0),
                                book.best_ask_ticks().value_or(0),
                                book.best_bid_quantity_units().value_or(0),
                                book.best_ask_quantity_units().value_or(0)
                            );

                            if (on_update) on_update(asset_id, book);
                        }
                    }
                }
            }
        } catch (...) {}
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_wall_seconds = std::chrono::duration<double>(end_time - start_time).count();
    if (result.elapsed_wall_seconds > 0.0) {
        result.updates_per_sec = result.total_updates / result.elapsed_wall_seconds;
    }

    // Compute canonical final book state hashes
    for (const auto& [asset_id, book] : result.final_books) {
        result.final_book_hashes[asset_id] = StateHasher::hash_book_state(book);
    }

    return result;
}

} // namespace polymarket
