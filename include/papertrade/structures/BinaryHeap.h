#pragma once
//
// BinaryHeap.h — array-backed binary heap base (spec §4 row 9).
//
// Holds the shared machinery for MinHeap/MaxHeap: a DynamicArray<T> in heap
// order plus the sift-up / sift-down operations. The ONLY thing subclasses
// change is `prior(a, b)` — "should a sit closer to the root than b?" MinHeap
// answers with less-than, MaxHeap with greater-than. Same storage, same
// algorithms, one virtual predicate = the inheritance + polymorphism pair of the
// OOP checklist.
//
// An optional comparator lets domain code heap by a field (e.g. day % change for
// Top-Gainers) instead of the natural order of T.
//
// Complexity: push and pop are O(log n); peek O(1).
//
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/Heap.h"

namespace papertrade {

template <typename T>
class BinaryHeap : public Heap<T> {
public:
    // Strict weak ordering over T; defaults to operator<.
    using Less = std::function<bool(const T&, const T&)>;

    explicit BinaryHeap(Less less = [](const T& a, const T& b) { return a < b; })
        : less_(std::move(less)) {}

    void push(const T& value) override {
        data_.push_back(value);
        siftUp(data_.size() - 1);
    }

    T pop() override {
        if (data_.empty()) throw std::out_of_range("pop on empty heap");
        T root = std::move(data_[0]);
        const std::size_t last = data_.size() - 1;
        if (last > 0) data_[0] = std::move(data_[last]);
        data_.pop_back();
        if (!data_.empty()) siftDown(0);
        return root;
    }

    const T& peek() const override {
        if (data_.empty()) throw std::out_of_range("peek on empty heap");
        return data_[0];
    }

    bool empty() const override { return data_.empty(); }
    std::size_t size() const override { return data_.size(); }
    void clear() override { data_.clear(); }

protected:
    // True if `a` outranks `b` (belongs nearer the root). The sole point of
    // difference between MinHeap and MaxHeap.
    virtual bool prior(const T& a, const T& b) const = 0;

    Less less_;

private:
    void siftUp(std::size_t i) {
        while (i > 0) {
            std::size_t parent = (i - 1) / 2;
            if (!prior(data_[i], data_[parent])) break;
            std::swap(data_[i], data_[parent]);
            i = parent;
        }
    }

    void siftDown(std::size_t i) {
        const std::size_t n = data_.size();
        while (true) {
            std::size_t best = i;
            std::size_t l = 2 * i + 1, r = 2 * i + 2;
            if (l < n && prior(data_[l], data_[best])) best = l;
            if (r < n && prior(data_[r], data_[best])) best = r;
            if (best == i) break;
            std::swap(data_[i], data_[best]);
            i = best;
        }
    }

    DynamicArray<T> data_;
};

}  // namespace papertrade
