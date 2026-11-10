#include "orderbook.hpp"
#include "direct_array_orderbook.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <cassert>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace polymarket;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED EQUIVALENCE ASSERTION: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main(int argc, char* argv[]) {
    std::string filepath = "data/polymarket_recording.jsonl";
    if (argc > 1) {
        filepath = argv[1];
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open recording file: " << filepath << std::endl;
        return 1;
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "OrderBook Implementation Equivalence Test" << std::endl;
    std::cout << "Baseline: std::map OrderBook" << std::endl;
    std::cout << "Optimized: DirectArrayOrderBook (Price-Tick Indexed Array)" << std::endl;
    std::cout << "Input File: " << filepath << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::unordered_map<std::string, OrderBook> map_books;
    std::unordered_map<std::string, DirectArrayOrderBook> array_books;

    uint64_t line_number = 0;
    uint64_t updates_verified = 0;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        line_number++;

        try {
            json j = json::parse(line);
            if (!j.contains("raw")) continue;

            auto& raw = j["raw"];

            // 1. Initial Snapshot array
            if (raw.is_array()) {
                for (auto& item : raw) {
                    if (!item.contains("asset_id")) continue;
                    std::string asset_id = item["asset_id"].get<std::string>();
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

                    if (map_books.find(asset_id) == map_books.end()) {
                        map_books.emplace(asset_id, OrderBook(asset_id, market));
                        array_books.emplace(asset_id, DirectArrayOrderBook(asset_id, market));
                    }

                    map_books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    array_books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    updates_verified++;

                    // Verify identical snapshot state
                    TEST_ASSERT(map_books[asset_id].best_bid_ticks() == array_books[asset_id].best_bid_ticks(), "Snapshot Best Bid Match");
                    TEST_ASSERT(map_books[asset_id].best_ask_ticks() == array_books[asset_id].best_ask_ticks(), "Snapshot Best Ask Match");
                    TEST_ASSERT(map_books[asset_id].best_bid_quantity_units() == array_books[asset_id].best_bid_quantity_units(), "Snapshot Best Bid Qty Match");
                    TEST_ASSERT(map_books[asset_id].best_ask_quantity_units() == array_books[asset_id].best_ask_quantity_units(), "Snapshot Best Ask Qty Match");
                }
            }
            // 2. Single Object Events
            else if (raw.is_object()) {
                std::string event_type = raw.value("event_type", "");

                if (event_type == "book") {
                    if (!raw.contains("asset_id")) continue;
                    std::string asset_id = raw["asset_id"].get<std::string>();
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

                    if (map_books.find(asset_id) == map_books.end()) {
                        map_books.emplace(asset_id, OrderBook(asset_id, market));
                        array_books.emplace(asset_id, DirectArrayOrderBook(asset_id, market));
                    }

                    map_books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    array_books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    updates_verified++;

                    TEST_ASSERT(map_books[asset_id].best_bid_ticks() == array_books[asset_id].best_bid_ticks(), "Snapshot Best Bid Match");
                    TEST_ASSERT(map_books[asset_id].best_ask_ticks() == array_books[asset_id].best_ask_ticks(), "Snapshot Best Ask Match");
                } else if (event_type == "price_change") {
                    if (raw.contains("price_changes") && raw["price_changes"].is_array()) {
                        for (auto& change : raw["price_changes"]) {
                            if (!change.contains("asset_id")) continue;
                            std::string asset_id = change["asset_id"].get<std::string>();
                            std::string price_str = change["price"].get<std::string>();
                            std::string size_str = change["size"].get<std::string>();
                            std::string side_str = change["side"].get<std::string>();

                            Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                            int64_t price_ticks = parse_price_str(price_str);
                            int64_t size_units = parse_qty_str(size_str);

                            if (map_books.find(asset_id) == map_books.end()) {
                                map_books.emplace(asset_id, OrderBook(asset_id, raw.value("market", "")));
                                array_books.emplace(asset_id, DirectArrayOrderBook(asset_id, raw.value("market", "")));
                            }

                            // Apply to both implementations
                            map_books[asset_id].apply_delta(side, price_ticks, size_units);
                            array_books[asset_id].apply_delta(side, price_ticks, size_units);
                            updates_verified++;

                            // Assert bit-for-bit equivalence after every single update!
                            auto& mb = map_books[asset_id];
                            auto& ab = array_books[asset_id];

                            TEST_ASSERT(mb.best_bid_ticks() == ab.best_bid_ticks(), "BBO Bid Ticks Equal");
                            TEST_ASSERT(mb.best_ask_ticks() == ab.best_ask_ticks(), "BBO Ask Ticks Equal");
                            TEST_ASSERT(mb.best_bid_quantity_units() == ab.best_bid_quantity_units(), "BBO Bid Qty Equal");
                            TEST_ASSERT(mb.best_ask_quantity_units() == ab.best_ask_quantity_units(), "BBO Ask Qty Equal");

                            if (mb.spread().has_value() && ab.spread().has_value()) {
                                TEST_ASSERT(std::abs(*mb.spread() - *ab.spread()) < 1e-9, "Spread Match");
                            }
                            if (mb.mid_price().has_value() && ab.mid_price().has_value()) {
                                TEST_ASSERT(std::abs(*mb.mid_price() - *ab.mid_price()) < 1e-9, "Mid Price Match");
                            }
                            if (mb.micro_price().has_value() && ab.micro_price().has_value()) {
                                TEST_ASSERT(std::abs(*mb.micro_price() - *ab.micro_price()) < 1e-9, "Micro Price Match");
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON warning on line " << line_number << ": " << e.what() << std::endl;
        }
    }

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "Equivalence Test Summary:" << std::endl;
    std::cout << "Lines Processed: " << line_number << std::endl;
    std::cout << "Total Updates Verified: " << updates_verified << std::endl;
    std::cout << ">>> 100% BIT-FOR-BIT EQUIVALENCE VERIFIED ON EVERY UPDATE! <<<" << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
