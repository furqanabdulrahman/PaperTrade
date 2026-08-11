#pragma once
//
// Sorter.h — abstract sorting-strategy ADT (spec §4 row 10).
//
// Every algorithm (Bubble, Selection, Insertion, Merge, Quick, Heap, Counting,
// Radix) is a concrete Sorter<T> selected polymorphically at runtime by the
// /api/sort endpoint. Sorters operate in place on a DynamicArray<T> and count
// their own comparisons/operations so the demo can contrast comparison sorts
// (Weeks 11–12) against non-comparison sorts (Week 13) on identical data.
//
// This is the classic Strategy pattern: one interface, many interchangeable
// algorithms — the "abstraction + polymorphism" pair of the OOP checklist.
//
#include <cstddef>
#include <functional>

#include "papertrade/adt/DynamicArray.h"

namespace papertrade {

template <typename T>
class Sorter {
public:
    // Returns true if `a` should come before `b`.
    using Comparator = std::function<bool(const T& a, const T& b)>;

    virtual ~Sorter() = default;

    // Sort `data` in place according to `less`.
    virtual void sort(DynamicArray<T>& data, const Comparator& less) = 0;

    // Human-readable name for the benchmark/demo UI (e.g. "QuickSort").
    virtual const char* name() const = 0;

    // Instrumentation for the timing/comparison-count endpoint. Reset at the
    // start of each sort() call.
    virtual std::size_t comparisons() const = 0;
    virtual std::size_t moves() const = 0;
};

}  // namespace papertrade
