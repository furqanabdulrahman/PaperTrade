#pragma once
//
// MaxHeap.h — largest-on-top heap (spec §4 row 9).
//
// Powers Top-Gainers: heap by day % change so the best performer is always at
// the root. Differs from MinHeap only in `prior` (arguments swapped).
//
#include "papertrade/structures/BinaryHeap.h"

namespace papertrade {

template <typename T>
class MaxHeap : public BinaryHeap<T> {
public:
    using Less = typename BinaryHeap<T>::Less;
    using BinaryHeap<T>::BinaryHeap;  // inherit constructors (default + custom less)

protected:
    // "a outranks b" when a is GREATER, i.e. b < a under the ordering.
    bool prior(const T& a, const T& b) const override { return this->less_(b, a); }
};

}  // namespace papertrade
