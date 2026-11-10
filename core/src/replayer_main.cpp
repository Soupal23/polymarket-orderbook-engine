#include "replayer.hpp"
#include <iostream>
#include <iomanip>

using namespace polymarket;

int main(int argc, char* argv[]) {
    ReplayConfig config;
    config.filepath = "data/polymarket_recording.jsonl";
    config.speed_multiplier = 0.0; // max speed by default

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            config.filepath = argv[++i];
        } else if (arg == "--speed" && i + 1 < argc) {
            std::string sp = argv[++i];
            if (sp == "max" || sp == "0") config.speed_multiplier = 0.0;
            else if (sp == "1x" || sp == "1") config.speed_multiplier = 1.0;
            else if (sp == "10x" || sp == "10") config.speed_multiplier = 10.0;
            else config.speed_multiplier = std::stod(sp);
        } else if (arg == "--market" && i + 1 < argc) {
            config.filter_asset_id = argv[++i];
        }
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "Polymarket OrderBook Deterministic Replayer" << std::endl;
    std::cout << "Feed File: " << config.filepath << std::endl;
    std::cout << "Pacing: " << (config.speed_multiplier == 0.0 ? "MAX (As fast as possible)" : (std::to_string(config.speed_multiplier) + "x Real-Time")) << std::endl;
    if (!config.filter_asset_id.empty()) {
        std::cout << "Market Filter: " << config.filter_asset_id << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    Replayer replayer(config);
    auto result = replayer.run();

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "Replay Completed Successfully" << std::endl;
    std::cout << "Total Log Lines Read: " << result.total_lines << std::endl;
    std::cout << "Total OrderBook Updates: " << result.total_updates << std::endl;
    std::cout << "Wall-Clock Time: " << std::fixed << std::setprecision(4) << result.elapsed_wall_seconds << " s" << std::endl;
    std::cout << "Replay Throughput: " << std::fixed << std::setprecision(0) << result.updates_per_sec << " updates/sec" << std::endl;
    std::cout << "Running Output Stream Hash: " << StateHasher::to_hex(result.stream_hash) << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << "Final Book State Hashes by Market Token:" << std::endl;
    for (const auto& [asset_id, hash] : result.final_book_hashes) {
        const auto& book = result.final_books.at(asset_id);
        std::cout << "  - Asset " << asset_id.substr(0, 16) << "... : "
                  << StateHasher::to_hex(hash)
                  << std::fixed << std::setprecision(3)
                  << " (Bid: " << book.best_bid().value_or(0)
                  << ", Ask: " << book.best_ask().value_or(0)
                  << ", Spread: " << book.spread().value_or(0) << ")" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    return 0;
}
