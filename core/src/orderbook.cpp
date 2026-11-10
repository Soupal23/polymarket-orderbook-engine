#include "orderbook.hpp"
#include <algorithm>

namespace polymarket {

OrderBook::OrderBook(std::string asset_id, std::string market_id)
    : asset_id_(std::move(asset_id)),
      market_id_(std::move(market_id)) {}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    order_locations_.clear();
    last_hash_.clear();
    last_timestamp_ms_ = 0;
}

void OrderBook::apply_snapshot(
    const std::vector<std::pair<int64_t, int64_t>>& bids_ticks_qty,
    const std::vector<std::pair<int64_t, int64_t>>& asks_ticks_qty,
    const std::string& hash,
    uint64_t timestamp_ms
) {
    clear();
    last_hash_ = hash;
    last_timestamp_ms_ = timestamp_ms;

    for (const auto& [price, qty] : bids_ticks_qty) {
        if (qty > 0 && price >= 0) {
            bids_[price].set_aggregate_quantity(qty);
            bids_[price].price_ticks = price;
        }
    }

    for (const auto& [price, qty] : asks_ticks_qty) {
        if (qty > 0 && price >= 0) {
            asks_[price].set_aggregate_quantity(qty);
            asks_[price].price_ticks = price;
        }
    }
}

void OrderBook::update_level(Side side, int64_t price_ticks, int64_t size_units) {
    if (side == Side::BUY) {
        if (size_units <= 0) {
            bids_.erase(price_ticks);
        } else {
            auto& level = bids_[price_ticks];
            level.price_ticks = price_ticks;
            level.set_aggregate_quantity(size_units);
        }
    } else {
        if (size_units <= 0) {
            asks_.erase(price_ticks);
        } else {
            auto& level = asks_[price_ticks];
            level.price_ticks = price_ticks;
            level.set_aggregate_quantity(size_units);
        }
    }
}

std::vector<Trade> OrderBook::add_limit_order(Order order) {
    std::vector<Trade> trades;

    if (order.remaining_qty_units <= 0) {
        return trades;
    }

    if (order.side == Side::BUY) {
        // Cross against resting asks while order.price >= best_ask
        while (order.remaining_qty_units > 0 && !asks_.empty() && order.price_ticks >= asks_.begin()->first) {
            auto it = asks_.begin();
            int64_t match_price = it->first;
            auto& level = it->second;

            if (level.orders.empty()) {
                // Aggregated liquidity from L2 feed
                int64_t match_qty = std::min(order.remaining_qty_units, level.total_quantity_units);
                trades.push_back(Trade{
                    0, // Maker ID unknown in aggregate L2
                    order.order_id,
                    Side::BUY,
                    match_price,
                    match_qty,
                    order.timestamp_ns
                });

                order.remaining_qty_units -= match_qty;
                level.total_quantity_units -= match_qty;

                if (level.total_quantity_units <= 0) {
                    asks_.erase(it);
                }
            } else {
                // FIFO queue of resting maker orders
                while (order.remaining_qty_units > 0 && !level.orders.empty()) {
                    auto& maker = level.orders.front();
                    int64_t match_qty = std::min(order.remaining_qty_units, maker.remaining_qty_units);

                    trades.push_back(Trade{
                        maker.order_id,
                        order.order_id,
                        Side::BUY,
                        match_price,
                        match_qty,
                        order.timestamp_ns
                    });

                    order.remaining_qty_units -= match_qty;
                    maker.remaining_qty_units -= match_qty;
                    level.total_quantity_units -= match_qty;

                    if (maker.is_filled()) {
                        order_locations_.erase(maker.order_id);
                        level.orders.pop_front();
                    }
                }

                if (level.orders.empty() && level.total_quantity_units <= 0) {
                    asks_.erase(it);
                }
            }
        }

        // Rest remaining unfilled quantity in bids
        if (order.remaining_qty_units > 0) {
            auto& level = bids_[order.price_ticks];
            level.price_ticks = order.price_ticks;
            level.add_order(order);
            order_locations_[order.order_id] = {Side::BUY, order.price_ticks};
        }
    } else {
        // SELL order: Cross against resting bids while order.price <= best_bid
        while (order.remaining_qty_units > 0 && !bids_.empty() && order.price_ticks <= bids_.begin()->first) {
            auto it = bids_.begin();
            int64_t match_price = it->first;
            auto& level = it->second;

            if (level.orders.empty()) {
                int64_t match_qty = std::min(order.remaining_qty_units, level.total_quantity_units);
                trades.push_back(Trade{
                    0,
                    order.order_id,
                    Side::SELL,
                    match_price,
                    match_qty,
                    order.timestamp_ns
                });

                order.remaining_qty_units -= match_qty;
                level.total_quantity_units -= match_qty;

                if (level.total_quantity_units <= 0) {
                    bids_.erase(it);
                }
            } else {
                while (order.remaining_qty_units > 0 && !level.orders.empty()) {
                    auto& maker = level.orders.front();
                    int64_t match_qty = std::min(order.remaining_qty_units, maker.remaining_qty_units);

                    trades.push_back(Trade{
                        maker.order_id,
                        order.order_id,
                        Side::SELL,
                        match_price,
                        match_qty,
                        order.timestamp_ns
                    });

                    order.remaining_qty_units -= match_qty;
                    maker.remaining_qty_units -= match_qty;
                    level.total_quantity_units -= match_qty;

                    if (maker.is_filled()) {
                        order_locations_.erase(maker.order_id);
                        level.orders.pop_front();
                    }
                }

                if (level.orders.empty() && level.total_quantity_units <= 0) {
                    bids_.erase(it);
                }
            }
        }

        // Rest remaining unfilled quantity in asks
        if (order.remaining_qty_units > 0) {
            auto& level = asks_[order.price_ticks];
            level.price_ticks = order.price_ticks;
            level.add_order(order);
            order_locations_[order.order_id] = {Side::SELL, order.price_ticks};
        }
    }

    return trades;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto loc_it = order_locations_.find(order_id);
    if (loc_it == order_locations_.end()) {
        return false;
    }

    const OrderLocation loc = loc_it->second;
    order_locations_.erase(loc_it);

    if (loc.side == Side::BUY) {
        auto level_it = bids_.find(loc.price_ticks);
        if (level_it == bids_.end()) return false;

        auto& orders = level_it->second.orders;
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->order_id == order_id) {
                level_it->second.total_quantity_units -= it->remaining_qty_units;
                orders.erase(it);
                break;
            }
        }

        if (orders.empty() && level_it->second.total_quantity_units <= 0) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(loc.price_ticks);
        if (level_it == asks_.end()) return false;

        auto& orders = level_it->second.orders;
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->order_id == order_id) {
                level_it->second.total_quantity_units -= it->remaining_qty_units;
                orders.erase(it);
                break;
            }
        }

        if (orders.empty() && level_it->second.total_quantity_units <= 0) {
            asks_.erase(level_it);
        }
    }

    return true;
}

std::optional<int64_t> OrderBook::best_bid_ticks() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::best_ask_ticks() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<double> OrderBook::best_bid() const {
    auto ticks = best_bid_ticks();
    if (!ticks.has_value()) return std::nullopt;
    return ticks_to_price(*ticks);
}

std::optional<double> OrderBook::best_ask() const {
    auto ticks = best_ask_ticks();
    if (!ticks.has_value()) return std::nullopt;
    return ticks_to_price(*ticks);
}

std::optional<int64_t> OrderBook::best_bid_quantity_units() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->second.total_quantity_units;
}

std::optional<int64_t> OrderBook::best_ask_quantity_units() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->second.total_quantity_units;
}

std::optional<double> OrderBook::spread() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb.has_value() || !ba.has_value()) return std::nullopt;
    return *ba - *bb;
}

std::optional<double> OrderBook::mid_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb.has_value() || !ba.has_value()) return std::nullopt;
    return (*bb + *ba) / 2.0;
}

std::optional<double> OrderBook::micro_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    auto qb = best_bid_quantity_units();
    auto qa = best_ask_quantity_units();

    if (!bb.has_value() || !ba.has_value() || !qb.has_value() || !qa.has_value()) {
        return std::nullopt;
    }

    int64_t total_qty = *qb + *qa;
    if (total_qty == 0) {
        return mid_price();
    }

    // Micro-price weights best bid by ask size and best ask by bid size
    double w_bid = static_cast<double>(*qa) / static_cast<double>(total_qty);
    double w_ask = static_cast<double>(*qb) / static_cast<double>(total_qty);
    return (*bb * w_bid) + (*ba * w_ask);
}

bool OrderBook::is_crossed() const {
    auto bb = best_bid_ticks();
    auto ba = best_ask_ticks();
    if (!bb.has_value() || !ba.has_value()) return false;
    return *bb >= *ba;
}

int64_t OrderBook::total_bid_quantity_units() const {
    int64_t sum = 0;
    for (const auto& [price, level] : bids_) {
        sum += level.total_quantity_units;
    }
    return sum;
}

int64_t OrderBook::total_ask_quantity_units() const {
    int64_t sum = 0;
    for (const auto& [price, level] : asks_) {
        sum += level.total_quantity_units;
    }
    return sum;
}

std::vector<std::pair<double, double>> OrderBook::get_bids_l2(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    result.reserve(std::min(depth, bids_.size()));

    size_t count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        result.emplace_back(ticks_to_price(it->first), units_to_qty(it->second.total_quantity_units));
    }
    return result;
}

std::vector<std::pair<double, double>> OrderBook::get_asks_l2(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    result.reserve(std::min(depth, asks_.size()));

    size_t count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        result.emplace_back(ticks_to_price(it->first), units_to_qty(it->second.total_quantity_units));
    }
    return result;
}

} // namespace polymarket
