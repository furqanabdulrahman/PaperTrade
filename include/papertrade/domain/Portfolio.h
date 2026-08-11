#pragma once
//
// Portfolio.h — a single user's cash + holdings + order ledger (Phase 8).
//
// Ties several graded structures together:
//   * holdings  → SeparateChainingHashTable<ticker, Position>  (O(1) lookup)
//   * history   → DynamicArray<Order>                          (append-only log)
//   * undo      → OrderStack<UndoRecord>                       (LIFO reversal)
//
// Buys validate against cash and update the volume-weighted average cost; sells
// validate against share count and realise proceeds. undoLast() restores the
// exact pre-trade cash and position via a snapshot, so it is always correct
// regardless of averaging.
//
#include <string>
#include <utility>

#include "papertrade/domain/Order.h"
#include "papertrade/structures/OrderStack.h"
#include "papertrade/structures/SeparateChainingHashTable.h"
#include "papertrade/adt/DynamicArray.h"

namespace papertrade {

class Portfolio {
public:
    struct Result {
        bool ok = false;
        std::string message;
        Order order;
    };

    Portfolio() = default;
    explicit Portfolio(double startingCash) : cash_(startingCash) {}

    double cash() const { return cash_; }
    std::size_t orderCount() const { return history_.size(); }
    const Order& orderAt(std::size_t i) const { return history_[i]; }

    const Position* position(const std::string& ticker) const {
        return holdings_.get(ticker);
    }

    Result buy(const std::string& ticker, double qty, double price) {
        if (qty <= 0) return {false, "quantity must be positive", {}};
        const double cost = qty * price;
        if (cost > cash_) return {false, "insufficient cash", {}};

        snapshot(ticker);
        cash_ -= cost;
        Position& p = holdings_.getOrCreate(ticker);
        const double newQty = p.qty + qty;
        p.avgCost = (p.costBasis() + cost) / newQty;  // volume-weighted
        p.qty = newQty;
        return record(ticker, Side::Buy, qty, price);
    }

    Result sell(const std::string& ticker, double qty, double price) {
        if (qty <= 0) return {false, "quantity must be positive", {}};
        Position* p = holdings_.get(ticker);
        if (!p || p->qty < qty) return {false, "insufficient shares", {}};

        snapshot(ticker);
        cash_ += qty * price;
        p->qty -= qty;
        if (p->qty <= 1e-9) holdings_.remove(ticker);
        return record(ticker, Side::Sell, qty, price);
    }

    // Reverses the most recent trade; false if there is nothing to undo.
    bool undoLast() {
        if (undo_.empty()) return false;
        const UndoRecord rec = undo_.top();
        undo_.pop();
        cash_ = rec.prevCash;
        if (rec.hadPosition) {
            holdings_.put(rec.ticker, rec.prevPos);
        } else {
            holdings_.remove(rec.ticker);
        }
        if (!history_.empty()) history_.pop_back();
        return true;
    }

    // cash + marketed value of every holding, priced by `price(ticker)`.
    template <typename PriceFn>
    double marketValue(PriceFn price) const {
        double total = cash_;
        holdings_.forEach(
            [&](const std::string& ticker, const Position& p) { total += p.qty * price(ticker); });
        return total;
    }

private:
    struct UndoRecord {
        std::string ticker;
        double prevCash = 0.0;
        bool hadPosition = false;
        Position prevPos;
    };

    void snapshot(const std::string& ticker) {
        const Position* existing = holdings_.get(ticker);
        UndoRecord rec;
        rec.ticker = ticker;
        rec.prevCash = cash_;
        rec.hadPosition = existing != nullptr;
        if (existing) rec.prevPos = *existing;
        undo_.push(std::move(rec));
    }

    Result record(const std::string& ticker, Side side, double qty, double price) {
        Order o{nextId_++, ticker, side, qty, price};
        history_.push_back(o);
        return {true, "ok", o};
    }

    double cash_ = 100000.0;  // starting paper balance
    SeparateChainingHashTable<std::string, Position> holdings_;
    DynamicArray<Order> history_;
    OrderStack<UndoRecord> undo_;
    long nextId_ = 1;
};

}  // namespace papertrade
