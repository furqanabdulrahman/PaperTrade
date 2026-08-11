#pragma once
//
// InstrumentedSorter.h — shared base for every concrete Sorter (spec §4 row 10).
//
// Holds the comparison/move counters and the small helpers (`less`, `swapAt`)
// that all algorithms funnel through, so the benchmark view can contrast them on
// identical data. Concrete sorters derive from this and implement sort()+name();
// this is the Strategy pattern's common scaffolding — the counters are the only
// state, the algorithm is the varying behaviour.
//
// Counting convention: `comparisons()` = number of comparator invocations;
// `moves()` = number of element writes into the array (a swap = 3). Consistent
// across algorithms, so O(n^2) vs O(n log n) shows up clearly in the totals.
//
#include <cstddef>
#include <utility>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/Sorter.h"

namespace papertrade {

template <typename T>
class InstrumentedSorter : public Sorter<T> {
public:
    using Comparator = typename Sorter<T>::Comparator;

    std::size_t comparisons() const override { return comparisons_; }
    std::size_t moves() const override { return moves_; }

protected:
    void resetCounters() {
        comparisons_ = 0;
        moves_ = 0;
    }

    // Every comparison in the comparison-sorts goes through here so it is counted.
    bool less(const Comparator& lt, const T& a, const T& b) {
        ++comparisons_;
        return lt(a, b);
    }

    void swapAt(DynamicArray<T>& d, std::size_t i, std::size_t j) {
        T tmp = std::move(d[i]);
        d[i] = std::move(d[j]);
        d[j] = std::move(tmp);
        moves_ += 3;
    }

    void countMoves(std::size_t n) { moves_ += n; }

    std::size_t comparisons_ = 0;
    std::size_t moves_ = 0;
};

}  // namespace papertrade
