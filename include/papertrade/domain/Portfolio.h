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
#include <vector>

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
        realizedPnl_ += qty * (price - p->avgCost);  // booked gain/loss
        cash_ += qty * price;
        p->qty -= qty;
        if (p->qty <= 1e-9) holdings_.remove(ticker);
        return record(ticker, Side::Sell, qty, price);
    }

    double realizedPnl() const { return realizedPnl_; }

    // --- Limit orders -------------------------------------------------------
    // Rest an order that fills automatically once the market crosses `limit`.
    Result placeLimit(const std::string& ticker, Side side, double qty, double limit) {
        if (qty <= 0) return {false, "quantity must be positive", {}};
        if (limit <= 0) return {false, "limit price must be positive", {}};
        pending_.push_back({nextId_++, ticker, side, qty, limit});
        return {true, "limit order placed", {}};
    }

    std::size_t pendingCount() const { return pending_.size(); }
    const PendingOrder& pendingAt(std::size_t i) const { return pending_[i]; }

    bool cancelPending(long id) {
        for (std::size_t i = 0; i < pending_.size(); ++i) {
            if (pending_[i].id == id) {
                removePendingAt(i);
                return true;
            }
        }
        return false;
    }

    // Fill any resting limit orders whose trigger the current prices satisfy.
    // Returns the orders that executed this pass. Call it on every price tick.
    template <typename PriceFn>
    std::vector<Order> evaluate(PriceFn price) {
        std::vector<Order> filled;
        for (std::size_t i = pending_.size(); i-- > 0;) {
            const PendingOrder po = pending_[i];
            const double cur = price(po.ticker);
            if (cur <= 0.0) continue;
            const bool trigger = po.side == Side::Buy ? cur <= po.limitPrice
                                                      : cur >= po.limitPrice;
            if (!trigger) continue;
            const Result r = po.side == Side::Buy
                                 ? buy(po.ticker, po.qty, po.limitPrice)
                                 : sell(po.ticker, po.qty, po.limitPrice);
            if (r.ok) {
                filled.push_back(r.order);
                removePendingAt(i);
            }
            // On failure (e.g. not enough cash yet) the order stays resting.
        }
        return filled;
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

    // Order-agnostic swap-remove (pending order sequence is not significant).
    void removePendingAt(std::size_t i) {
        const std::size_t last = pending_.size() - 1;
        if (i != last) pending_[i] = pending_[last];
        pending_.pop_back();
    }

    double cash_ = 100000.0;  // starting paper balance
    double realizedPnl_ = 0.0;
    SeparateChainingHashTable<std::string, Position> holdings_;
    DynamicArray<Order> history_;
    DynamicArray<PendingOrder> pending_;
    OrderStack<UndoRecord> undo_;
    long nextId_ = 1;
};

}  // namespace papertrade
