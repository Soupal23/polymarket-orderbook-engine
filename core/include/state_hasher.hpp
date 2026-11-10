#pragma once

#include "orderbook.hpp"
#include <cstdint>
#include <string>
#include <iomanip>
#include <sstream>

namespace polymarket {

// Deterministic 64-bit FNV-1a Hash Implementation
class StateHasher {
public:
    static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

    static uint64_t hash_int64(uint64_t hash, int64_t val) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
        for (size_t i = 0; i < sizeof(int64_t); ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    static uint64_t hash_string(uint64_t hash, const std::string& str) {
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // Hashes full L2 canonical book state: (price, size) for all bids and asks
    static uint64_t hash_book_state(const OrderBook& book) {
        uint64_t hash = FNV_OFFSET_BASIS;
        hash = hash_string(hash, book.get_asset_id());

        // Hash bids in strict descending price order
        auto bids = book.get_bids_l2(1000);
        for (const auto& [p, q] : bids) {
            hash = hash_int64(hash, price_to_ticks(p));
            hash = hash_int64(hash, qty_to_units(q));
        }

        // Hash asks in strict ascending price order
        auto asks = book.get_asks_l2(1000);
        for (const auto& [p, q] : asks) {
            hash = hash_int64(hash, price_to_ticks(p));
            hash = hash_int64(hash, qty_to_units(q));
        }

        return hash;
    }

    // Updates a rolling output stream digest with every emitted BBO update
    static void update_stream_hash(uint64_t& running_hash, int64_t bb_ticks, int64_t ba_ticks, int64_t bb_qty, int64_t ba_qty) {
        running_hash = hash_int64(running_hash, bb_ticks);
        running_hash = hash_int64(running_hash, ba_ticks);
        running_hash = hash_int64(running_hash, bb_qty);
        running_hash = hash_int64(running_hash, ba_qty);
    }

    static std::string to_hex(uint64_t hash) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << hash;
        return ss.str();
    }
};

} // namespace polymarket
