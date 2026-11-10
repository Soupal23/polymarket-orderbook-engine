#pragma once

#include "types.hpp"
#include <cstdint>
#include <string>

namespace polymarket {

struct Order {
    uint64_t order_id;
    Side side;
    int64_t price_ticks;       // Scaled by PRICE_SCALE (10000)
    int64_t original_qty_units;// Scaled by QTY_SCALE (10000)
    int64_t remaining_qty_units;
    uint64_t timestamp_ns;

    Order(uint64_t id, Side s, int64_t price, int64_t qty, uint64_t ts = 0)
        : order_id(id),
          side(s),
          price_ticks(price),
          original_qty_units(qty),
          remaining_qty_units(qty),
          timestamp_ns(ts) {}

    bool is_filled() const {
        return remaining_qty_units <= 0;
    }
};

struct Trade {
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    Side taker_side;
    int64_t price_ticks;
    int64_t quantity_units;
    uint64_t timestamp_ns;
};

} // namespace polymarket
