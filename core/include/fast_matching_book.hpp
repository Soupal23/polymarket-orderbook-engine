#pragma once

#include "types.hpp"
#include "order.hpp"
#include <list>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace polymarket {

struct FastPriceLevel {
    int64_t price_ticks{0};
    int64_t total_quantity_units{0};
    std::list<Order> orders; // Doubly linked list for stable iterators and O(1) removal

    explicit FastPriceLevel(int64_t price = 0) : price_ticks(price), total_quantity_units(0) {}
};

class FastMatchingBook {
public:
    FastMatchingBook() = default;

    // Add resting limit order and record iterator in hash map
    void add_limit_order(const Order& order) {
        if (order.side == Side::BUY) {
            auto& level = bids_[order.price_ticks];
            level.price_ticks = order.price_ticks;
            level.total_quantity_units += order.remaining_qty_units;
            level.orders.push_back(order);

            // Store iterator pointing directly to the newly inserted DLL node
            auto it = std::prev(level.orders.end());
            order_locations_[order.order_id] = {Side::BUY, order.price_ticks, it};
        } else {
            auto& level = asks_[order.price_ticks];
            level.price_ticks = order.price_ticks;
            level.total_quantity_units += order.remaining_qty_units;
            level.orders.push_back(order);

            auto it = std::prev(level.orders.end());
            order_locations_[order.order_id] = {Side::SELL, order.price_ticks, it};
        }
    }

    // True O(1) order cancellation using stored doubly linked list iterator
    bool cancel_order_fast(uint64_t order_id) {
        auto loc_it = order_locations_.find(order_id);
        if (loc_it == order_locations_.end()) return false;

        const auto& loc = loc_it->second;

        if (loc.side == Side::BUY) {
            auto level_it = bids_.find(loc.price_ticks);
            if (level_it == bids_.end()) return false;

            auto& level = level_it->second;
            level.total_quantity_units -= loc.iter->remaining_qty_units;

            // O(1) pointer rewiring erasure
            level.orders.erase(loc.iter);

            if (level.orders.empty() && level.total_quantity_units <= 0) {
                bids_.erase(level_it);
            }
        } else {
            auto level_it = asks_.find(loc.price_ticks);
            if (level_it == asks_.end()) return false;

            auto& level = level_it->second;
            level.total_quantity_units -= loc.iter->remaining_qty_units;

            level.orders.erase(loc.iter);

            if (level.orders.empty() && level.total_quantity_units <= 0) {
                asks_.erase(level_it);
            }
        }

        order_locations_.erase(loc_it);
        return true;
    }

    size_t order_count() const { return order_locations_.size(); }

private:
    struct OrderLocation {
        Side side;
        int64_t price_ticks;
        std::list<Order>::iterator iter; // Direct stable node pointer in DLL
    };

    std::map<int64_t, FastPriceLevel, std::greater<int64_t>> bids_;
    std::map<int64_t, FastPriceLevel, std::less<int64_t>> asks_;
    std::unordered_map<uint64_t, OrderLocation> order_locations_;
};

} // namespace polymarket
