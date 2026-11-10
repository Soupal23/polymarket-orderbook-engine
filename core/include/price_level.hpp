#pragma once

#include "types.hpp"
#include "order.hpp"
#include <deque>
#include <cstdint>

namespace polymarket {

struct PriceLevel {
    int64_t price_ticks;
    int64_t total_quantity_units{0};
    std::deque<Order> orders; // FIFO queue for individual order matching

    explicit PriceLevel(int64_t price = 0) : price_ticks(price), total_quantity_units(0) {}

    void add_order(const Order& order) {
        orders.push_back(order);
        total_quantity_units += order.remaining_qty_units;
    }

    // Direct level update (e.g. from Polymarket L2 price_change feed)
    void set_aggregate_quantity(int64_t qty_units) {
        total_quantity_units = qty_units;
        // When setting aggregate directly from L2 feed, synthetic orders are cleared
        orders.clear();
    }
};

} // namespace polymarket
