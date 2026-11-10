#include "replayer.hpp"
#include <iostream>
#include <cassert>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "DETERMINISM FAILED: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace polymarket;

int main(int argc, char* argv[]) {
    std::string filepath = "data/polymarket_recording.jsonl";
    if (argc > 1) filepath = argv[1];

    std::cout << "==========================================================" << std::endl;
    std::cout << "Starting OrderBook Deterministic Replay Verification" << std::endl;
    std::cout << "Input File: " << filepath << std::endl;
    std::cout << "==========================================================" << std::endl;

    ReplayConfig config;
    config.filepath = filepath;
    config.speed_multiplier = 0.0; // Max speed

    Replayer replayer(config);

    // Pass 1
    std::cout << "[PASS 1] Executing initial replay pass..." << std::endl;
    auto res1 = replayer.run();
    std::cout << "  Lines: " << res1.total_lines << " | Updates: " << res1.total_updates << std::endl;
    std::cout << "  Stream Digest: " << StateHasher::to_hex(res1.stream_hash) << std::endl;

    // Pass 2
    std::cout << "[PASS 2] Executing second independent replay pass..." << std::endl;
    auto res2 = replayer.run();
    std::cout << "  Lines: " << res2.total_lines << " | Updates: " << res2.total_updates << std::endl;
    std::cout << "  Stream Digest: " << StateHasher::to_hex(res2.stream_hash) << std::endl;

    // 1. Output stream hash equality assertion
    TEST_ASSERT(res1.stream_hash == res2.stream_hash, "Running output stream hash must match bit-for-bit");

    // 2. Total update counts assertion
    TEST_ASSERT(res1.total_lines == res2.total_lines, "Total log line count must match");
    TEST_ASSERT(res1.total_updates == res2.total_updates, "Total update count must match");

    // 3. Final Book state hashes assertion
    TEST_ASSERT(res1.final_book_hashes.size() == res2.final_book_hashes.size(), "Book count must match");
    for (const auto& [asset_id, hash1] : res1.final_book_hashes) {
        TEST_ASSERT(res2.final_book_hashes.count(asset_id) > 0, "Asset must exist in both passes");
        uint64_t hash2 = res2.final_book_hashes.at(asset_id);
        TEST_ASSERT(hash1 == hash2, "Book state hash must match for asset: " + asset_id);

        const auto& b1 = res1.final_books.at(asset_id);
        const auto& b2 = res2.final_books.at(asset_id);
        TEST_ASSERT(b1 == b2, "OrderBook internal state must be bit-for-bit equal");
    }

    std::cout << "\n==========================================================" << std::endl;
    std::cout << ">>> DETERMINISTIC REPLAY TEST PASSED WITH 100% BIT EQUALITY! <<<" << std::endl;
    std::cout << "Stream Hash Pass 1: " << StateHasher::to_hex(res1.stream_hash) << std::endl;
    std::cout << "Stream Hash Pass 2: " << StateHasher::to_hex(res2.stream_hash) << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
