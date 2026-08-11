#pragma once
//
// Heap.h — abstract binary-heap ADT (spec §4 row 9), array-backed via
// DynamicArray<T>. MinHeap<T> and MaxHeap<T> both inherit from Heap<T> and
// differ only in the ordering predicate — the "inheritance + polymorphism" pair
// of the OOP checklist. Powers Top-Gainers / Top-Losers via partial heap-select
// (O(log n) push/pop, O(k log n) top-k — not a full O(n log n) sort).
//
#include <cstddef>

namespace papertrade {

template <typename T>
class Heap {
public:
    virtual ~Heap() = default;

    virtual void push(const T& value) = 0;
    virtual T pop() = 0;               // remove and return the root
    virtual const T& peek() const = 0;  // inspect the root

    virtual bool empty() const = 0;
    virtual std::size_t size() const = 0;
    virtual void clear() = 0;
};

}  // namespace papertrade
