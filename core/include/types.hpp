#pragma once

#include <cstdint>
#include <string>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace polymarket {

// Price in Polymarket CLOB is between 0.00 and 1.00 (probability/cents).
// We use a fixed-point integer representation with scale factor 10,000 (4 decimals).
// Example: $0.5200 is represented as 5200.
// This completely avoids IEEE 754 floating-point rounding issues in orderbook indexing.
constexpr int64_t PRICE_SCALE = 10000;

// Quantity scale factor: allows fractional shares down to 4 decimal places (e.g. 1500.5 -> 15,005,000 if 10^4).
constexpr int64_t QTY_SCALE = 10000;

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

inline const char* to_string(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

inline Side side_from_string(const std::string& str) {
    if (str == "BUY" || str == "buy" || str == "B") return Side::BUY;
    if (str == "SELL" || str == "sell" || str == "S") return Side::SELL;
    throw std::invalid_argument("Invalid side string: " + str);
}

// Fixed-point Price helper functions
inline int64_t price_to_ticks(double price) {
    if (price < 0.0 || price > 1.0000001) {
        throw std::out_of_range("Polymarket price must be within [0.0, 1.0]");
    }
    return static_cast<int64_t>(std::round(price * PRICE_SCALE));
}

inline double ticks_to_price(int64_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(PRICE_SCALE);
}

inline int64_t qty_to_units(double qty) {
    if (qty < 0.0) {
        throw std::invalid_argument("Quantity cannot be negative");
    }
    return static_cast<int64_t>(std::round(qty * QTY_SCALE));
}

inline double units_to_qty(int64_t units) {
    return static_cast<double>(units) / static_cast<double>(QTY_SCALE);
}

// Convert string price ("0.52") directly to integer ticks without float drift
inline int64_t parse_price_str(const std::string& str) {
    size_t dot_pos = str.find('.');
    if (dot_pos == std::string::npos) {
        int64_t whole = std::stoll(str);
        return whole * PRICE_SCALE;
    }
    int64_t whole = dot_pos > 0 ? std::stoll(str.substr(0, dot_pos)) : 0;
    std::string frac_str = str.substr(dot_pos + 1);
    if (frac_str.size() > 4) {
        frac_str = frac_str.substr(0, 4);
    } else {
        frac_str.append(4 - frac_str.size(), '0');
    }
    int64_t frac = std::stoll(frac_str);
    return whole * PRICE_SCALE + frac;
}

// Convert string quantity ("1500.5") to integer units
inline int64_t parse_qty_str(const std::string& str) {
    size_t dot_pos = str.find('.');
    if (dot_pos == std::string::npos) {
        int64_t whole = std::stoll(str);
        return whole * QTY_SCALE;
    }
    int64_t whole = dot_pos > 0 ? std::stoll(str.substr(0, dot_pos)) : 0;
    std::string frac_str = str.substr(dot_pos + 1);
    if (frac_str.size() > 4) {
        frac_str = frac_str.substr(0, 4);
    } else {
        frac_str.append(4 - frac_str.size(), '0');
    }
    int64_t frac = std::stoll(frac_str);
    return whole * QTY_SCALE + frac;
}

} // namespace polymarket
