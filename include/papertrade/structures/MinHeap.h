#pragma once
//
// MinHeap.h — smallest-on-top heap (spec §4 row 9).
//
// Powers Top-Losers: heap by day % change so the worst performer is always at
// the root, popped in O(log n). Differs from MaxHeap only in `prior`.
//
#include "papertrade/structures/BinaryHeap.h"

namespace papertrade {

template <typename T>
class MinHeap : public BinaryHeap<T> {
public:
    using Less = typename BinaryHeap<T>::Less;
    using BinaryHeap<T>::BinaryHeap;  // inherit constructors (default + custom less)

protected:
    bool prior(const T& a, const T& b) const override { return this->less_(a, b); }
};

}  // namespace papertrade
