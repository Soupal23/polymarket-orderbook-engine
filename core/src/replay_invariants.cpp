#include "orderbook.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace polymarket;

struct InvariantViolation {
    uint64_t line_number;
    std::string asset_id;
    std::string event_type;
    std::string details;
};

int main(int argc, char* argv[]) {
    std::string filepath = "data/polymarket_recording.jsonl";
    if (argc > 1) {
        filepath = argv[1];
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open recording file: " << filepath << std::endl;
        return 1;
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "Polymarket OrderBook Offline Invariant Replayer" << std::endl;
    std::cout << "Replaying File: " << filepath << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::unordered_map<std::string, OrderBook> books;
    std::vector<InvariantViolation> violations;

    uint64_t line_number = 0;
    uint64_t total_snapshots = 0;
    uint64_t total_deltas = 0;
    uint64_t total_invariant_checks = 0;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        line_number++;

        try {
            json j = json::parse(line);
            if (!j.contains("raw")) continue;

            auto& raw = j["raw"];

            // Case A: Initial dump can be an array of book snapshots
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
                            std::string p_str = b["price"].get<std::string>();
                            std::string s_str = b["size"].get<std::string>();
                            bids.emplace_back(parse_price_str(p_str), parse_qty_str(s_str));
                        }
                    }

                    std::vector<std::pair<int64_t, int64_t>> asks;
                    if (item.contains("asks") && item["asks"].is_array()) {
                        for (auto& a : item["asks"]) {
                            std::string p_str = a["price"].get<std::string>();
                            std::string s_str = a["size"].get<std::string>();
                            asks.emplace_back(parse_price_str(p_str), parse_qty_str(s_str));
                        }
                    }

                    if (books.find(asset_id) == books.end()) {
                        books.emplace(asset_id, OrderBook(asset_id, market));
                    }

                    books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    total_snapshots++;

                    // Invariant check on snapshot
                    total_invariant_checks++;
                    auto status = books[asset_id].check_invariants();
                    if (!status.is_valid) {
                        violations.push_back({line_number, asset_id, "snapshot_array", status.error_detail});
                    }
                }
            } 
            // Case B: Standard single object event
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

                    if (books.find(asset_id) == books.end()) {
                        books.emplace(asset_id, OrderBook(asset_id, market));
                    }

                    books[asset_id].apply_snapshot(bids, asks, hash, ts);
                    total_snapshots++;

                    total_invariant_checks++;
                    auto status = books[asset_id].check_invariants();
                    if (!status.is_valid) {
                        violations.push_back({line_number, asset_id, "book", status.error_detail});
                    }
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

                            if (books.find(asset_id) == books.end()) {
                                books.emplace(asset_id, OrderBook(asset_id, raw.value("market", "")));
                            }

                            books[asset_id].apply_delta(side, price_ticks, size_units);
                            total_deltas++;

                            // Verify invariants after every single delta
                            total_invariant_checks++;
                            auto status = books[asset_id].check_invariants();
                            if (!status.is_valid) {
                                violations.push_back({line_number, asset_id, "price_change", status.error_detail});
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON parse warning on line " << line_number << ": " << e.what() << std::endl;
        }
    }

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "Replay Summary Report" << std::endl;
    std::cout << "Total Log Lines Replayed: " << line_number << std::endl;
    std::cout << "Active Markets / Books Tracked: " << books.size() << std::endl;
    std::cout << "Total Snapshots Processed: " << total_snapshots << std::endl;
    std::cout << "Total Incremental Level Deltas Applied: " << total_deltas << std::endl;
    std::cout << "Total Invariant Checks Performed: " << total_invariant_checks << std::endl;
    std::cout << "Total Invariant Violations Detected: " << violations.size() << std::endl;
    std::cout << "==========================================================" << std::endl;

    if (!violations.empty()) {
        std::cout << "\n[ALERT] Invariant Violations Breakdown:" << std::endl;
        for (size_t i = 0; i < std::min<size_t>(violations.size(), 10); ++i) {
            std::cout << "  - Line " << violations[i].line_number
                      << " | Asset: " << violations[i].asset_id.substr(0, 12) << "..."
                      << " | Event: " << violations[i].event_type
                      << " | Error: " << violations[i].details << std::endl;
        }
        if (violations.size() > 10) {
            std::cout << "  ... and " << (violations.size() - 10) << " more violations." << std::endl;
        }
    } else {
        std::cout << "\n>>> ALL INVARIANT CHECKS PASSED WITH ZERO VIOLATIONS! <<<" << std::endl;
    }

    std::cout << "\n----------------------------------------------------------" << std::endl;
    std::cout << "Final Market States (Top-of-Book & Signals):" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
    for (const auto& [asset_id, book] : books) {
        auto bb = book.best_bid();
        auto ba = book.best_ask();
        auto sp = book.spread();
        auto mid = book.mid_price();
        auto micro = book.micro_price();
        auto imb = book.book_imbalance(1);

        std::cout << "Asset: " << asset_id.substr(0, 16) << "..."
                  << " | Bids: " << book.bid_depth_levels()
                  << " | Asks: " << book.ask_depth_levels() << "\n"
                  << "  Best Bid: " << (bb ? std::to_string(*bb) : "N/A")
                  << " | Best Ask: " << (ba ? std::to_string(*ba) : "N/A")
                  << " | Spread: " << (sp ? std::to_string(*sp) : "N/A") << "\n"
                  << "  Mid: " << (mid ? std::to_string(*mid) : "N/A")
                  << " | Micro: " << (micro ? std::to_string(*micro) : "N/A")
                  << " | Imbalance(1): " << (imb ? std::to_string(*imb) : "N/A") << "\n"
                  << std::endl;
    }

    return violations.empty() ? 0 : 1;
}
