#include "direct_array_orderbook.hpp"

namespace polymarket {

DirectArrayOrderBook::DirectArrayOrderBook(std::string asset_id, std::string market_id)
    : asset_id_(std::move(asset_id)),
      market_id_(std::move(market_id)) {
    clear();
}

void DirectArrayOrderBook::clear() {
    bid_levels_.fill(0);
    ask_levels_.fill(0);
    best_bid_tick_ = -1;
    best_ask_tick_ = static_cast<int64_t>(MAX_TICKS);
    active_bid_count_ = 0;
    active_ask_count_ = 0;
    last_hash_.clear();
    last_timestamp_ms_ = 0;
}

void DirectArrayOrderBook::apply_snapshot(
    const std::vector<std::pair<int64_t, int64_t>>& bids_ticks_qty,
    const std::vector<std::pair<int64_t, int64_t>>& asks_ticks_qty,
    const std::string& hash,
    uint64_t timestamp_ms
) {
    clear();
    last_hash_ = hash;
    last_timestamp_ms_ = timestamp_ms;

    for (const auto& [price, qty] : bids_ticks_qty) {
        if (price >= 0 && price < static_cast<int64_t>(MAX_TICKS) && qty > 0) {
            bid_levels_[price] = qty;
            active_bid_count_++;
            if (price > best_bid_tick_) {
                best_bid_tick_ = price;
            }
        }
    }

    for (const auto& [price, qty] : asks_ticks_qty) {
        if (price >= 0 && price < static_cast<int64_t>(MAX_TICKS) && qty > 0) {
            ask_levels_[price] = qty;
            active_ask_count_++;
            if (price < best_ask_tick_) {
                best_ask_tick_ = price;
            }
        }
    }
}

void DirectArrayOrderBook::recompute_best_bid() {
    for (int64_t p = best_bid_tick_ - 1; p >= 0; --p) {
        if (bid_levels_[p] > 0) {
            best_bid_tick_ = p;
            return;
        }
    }
    best_bid_tick_ = -1;
}

void DirectArrayOrderBook::recompute_best_ask() {
    for (int64_t p = best_ask_tick_ + 1; p < static_cast<int64_t>(MAX_TICKS); ++p) {
        if (ask_levels_[p] > 0) {
            best_ask_tick_ = p;
            return;
        }
    }
    best_ask_tick_ = static_cast<int64_t>(MAX_TICKS);
}

bool DirectArrayOrderBook::update_level(Side side, int64_t price_ticks, int64_t size_units, uint64_t timestamp_ms) {
    if (price_ticks < 0 || price_ticks >= static_cast<int64_t>(MAX_TICKS)) {
        return false;
    }

    if (timestamp_ms > last_timestamp_ms_) {
        last_timestamp_ms_ = timestamp_ms;
    }

    if (side == Side::BUY) {
        int64_t prev_qty = bid_levels_[price_ticks];
        if (prev_qty == 0 && size_units > 0) {
            active_bid_count_++;
        } else if (prev_qty > 0 && size_units <= 0) {
            active_bid_count_--;
        }

        bid_levels_[price_ticks] = (size_units > 0) ? size_units : 0;

        if (size_units > 0) {
            if (price_ticks > best_bid_tick_) {
                best_bid_tick_ = price_ticks;
            }
        } else if (price_ticks == best_bid_tick_) {
            recompute_best_bid();
        }
    } else {
        int64_t prev_qty = ask_levels_[price_ticks];
        if (prev_qty == 0 && size_units > 0) {
            active_ask_count_++;
        } else if (prev_qty > 0 && size_units <= 0) {
            active_ask_count_--;
        }

        ask_levels_[price_ticks] = (size_units > 0) ? size_units : 0;

        if (size_units > 0) {
            if (price_ticks < best_ask_tick_) {
                best_ask_tick_ = price_ticks;
            }
        } else if (price_ticks == best_ask_tick_) {
            recompute_best_ask();
        }
    }

    return true;
}

std::optional<int64_t> DirectArrayOrderBook::best_bid_ticks() const {
    if (best_bid_tick_ < 0) return std::nullopt;
    return best_bid_tick_;
}

std::optional<int64_t> DirectArrayOrderBook::best_ask_ticks() const {
    if (best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) return std::nullopt;
    return best_ask_tick_;
}

std::optional<double> DirectArrayOrderBook::best_bid() const {
    if (best_bid_tick_ < 0) return std::nullopt;
    return ticks_to_price(best_bid_tick_);
}

std::optional<double> DirectArrayOrderBook::best_ask() const {
    if (best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) return std::nullopt;
    return ticks_to_price(best_ask_tick_);
}

std::optional<int64_t> DirectArrayOrderBook::best_bid_quantity_units() const {
    if (best_bid_tick_ < 0) return std::nullopt;
    return bid_levels_[best_bid_tick_];
}

std::optional<int64_t> DirectArrayOrderBook::best_ask_quantity_units() const {
    if (best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) return std::nullopt;
    return ask_levels_[best_ask_tick_];
}

std::optional<double> DirectArrayOrderBook::spread() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb.has_value() || !ba.has_value()) return std::nullopt;
    return *ba - *bb;
}

std::optional<double> DirectArrayOrderBook::mid_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb.has_value() || !ba.has_value()) return std::nullopt;
    return (*bb + *ba) / 2.0;
}

std::optional<double> DirectArrayOrderBook::micro_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    auto qb = best_bid_quantity_units();
    auto qa = best_ask_quantity_units();

    if (!bb.has_value() || !ba.has_value() || !qb.has_value() || !qa.has_value()) {
        return std::nullopt;
    }

    int64_t total_qty = *qb + *qa;
    if (total_qty == 0) return mid_price();

    double w_bid = static_cast<double>(*qa) / static_cast<double>(total_qty);
    double w_ask = static_cast<double>(*qb) / static_cast<double>(total_qty);
    return (*bb * w_bid) + (*ba * w_ask);
}

std::optional<double> DirectArrayOrderBook::book_imbalance(size_t depth) const {
    if (best_bid_tick_ < 0 && best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) {
        return std::nullopt;
    }

    int64_t bid_vol = 0;
    size_t count = 0;
    for (int64_t p = best_bid_tick_; p >= 0 && count < depth; --p) {
        if (bid_levels_[p] > 0) {
            bid_vol += bid_levels_[p];
            count++;
        }
    }

    int64_t ask_vol = 0;
    count = 0;
    for (int64_t p = best_ask_tick_; p < static_cast<int64_t>(MAX_TICKS) && count < depth; ++p) {
        if (ask_levels_[p] > 0) {
            ask_vol += ask_levels_[p];
            count++;
        }
    }

    int64_t total = bid_vol + ask_vol;
    if (total == 0) return 0.0;
    return static_cast<double>(bid_vol - ask_vol) / static_cast<double>(total);
}

bool DirectArrayOrderBook::is_crossed() const {
    if (best_bid_tick_ < 0 || best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) {
        return false;
    }
    return best_bid_tick_ >= best_ask_tick_;
}

InvariantStatus DirectArrayOrderBook::check_invariants() const {
    if (is_crossed()) {
        return {
            false,
            "Crossed book: best_bid (" + std::to_string(best_bid().value_or(0)) +
            ") >= best_ask (" + std::to_string(best_ask().value_or(0)) + ")"
        };
    }
    return {true, ""};
}

std::vector<std::pair<double, double>> DirectArrayOrderBook::get_bids_l2(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    if (best_bid_tick_ < 0) return result;
    result.reserve(depth);

    size_t count = 0;
    for (int64_t p = best_bid_tick_; p >= 0 && count < depth; --p) {
        if (bid_levels_[p] > 0) {
            result.emplace_back(ticks_to_price(p), units_to_qty(bid_levels_[p]));
            count++;
        }
    }
    return result;
}

std::vector<std::pair<double, double>> DirectArrayOrderBook::get_asks_l2(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    if (best_ask_tick_ >= static_cast<int64_t>(MAX_TICKS)) return result;
    result.reserve(depth);

    size_t count = 0;
    for (int64_t p = best_ask_tick_; p < static_cast<int64_t>(MAX_TICKS) && count < depth; ++p) {
        if (ask_levels_[p] > 0) {
            result.emplace_back(ticks_to_price(p), units_to_qty(ask_levels_[p]));
            count++;
        }
    }
    return result;
}

} // namespace polymarket
