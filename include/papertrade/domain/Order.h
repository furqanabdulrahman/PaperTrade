#pragma once
//
// Order.h — core trading value types (spec §2, Phase 8).
//
#include <string>

namespace papertrade {

enum class Side { Buy, Sell };
enum class OrderType { Market, Limit };

inline const char* toString(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

// A resting limit order: fills automatically when the market crosses the limit
// price (buy when price <= limit, sell when price >= limit).
struct PendingOrder {
    long id = 0;
    std::string ticker;
    Side side = Side::Buy;
    double qty = 0.0;
    double limitPrice = 0.0;
};

// A single executed order in the paper-trading ledger.
struct Order {
    long id = 0;
    std::string ticker;
    Side side = Side::Buy;
    double qty = 0.0;
    double price = 0.0;

    double notional() const { return qty * price; }
};

// A net holding in one ticker: share count and volume-weighted average cost.
struct Position {
    double qty = 0.0;
    double avgCost = 0.0;

    double costBasis() const { return qty * avgCost; }
};

}  // namespace papertrade
