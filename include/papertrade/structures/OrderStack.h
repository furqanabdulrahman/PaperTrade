#pragma once
//
// OrderStack.h — concrete LIFO Stack<T> (spec §4 row 3).
//
// Per-user; stores each EXECUTED order so "undo last action" can pop the most
// recent one and apply its inverse trade (spec §5 undo semantics).
//
// Backing choice (documented per §4): ARRAY-backed via our own DynamicArray<T>.
// A stack only ever pushes/pops at one end, which is exactly DynamicArray's
// amortized-O(1) back operations, and contiguous storage is cache-friendlier
// than per-node allocation. No middle insert/erase is ever needed, so the
// linked-list advantage doesn't apply here.
//
#include <cstddef>
#include <stdexcept>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/Stack.h"

namespace papertrade {

template <typename T>
class OrderStack : public Stack<T> {
public:
    void push(const T& value) override { data_.push_back(value); }

    bool pop() override {
        if (data_.empty()) return false;
        data_.pop_back();
        return true;
    }

    T& top() override {
        if (data_.empty()) throw std::out_of_range("top() on empty OrderStack");
        return data_.back();
    }
    const T& top() const override {
        if (data_.empty()) throw std::out_of_range("top() on empty OrderStack");
        return data_.back();
    }

    bool empty() const override { return data_.empty(); }
    std::size_t size() const override { return data_.size(); }

private:
    DynamicArray<T> data_;
};

}  // namespace papertrade
