#pragma once
//
// ComparisonSorts.h — the six comparison-based Sorter<T> strategies
// (Weeks 11-12): Bubble, Selection, Insertion, Merge, Quick, Heap.
//
// All decide ordering solely through the injected comparator, so they sort any T
// and honour custom orderings (e.g. by day % change). Each resets its counters
// on entry and routes comparisons/moves through InstrumentedSorter so the demo
// can rank them on identical data.
//
#include <cstddef>
#include <utility>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/structures/sorters/InstrumentedSorter.h"

namespace papertrade {

// --- BubbleSort — O(n^2), early-exit when a pass makes no swaps -------------
template <typename T>
class BubbleSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "BubbleSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        const std::size_t n = d.size();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            bool swapped = false;
            for (std::size_t j = 0; j + 1 < n - i; ++j) {
                if (this->less(lt, d[j + 1], d[j])) {
                    this->swapAt(d, j, j + 1);
                    swapped = true;
                }
            }
            if (!swapped) break;
        }
    }
};

// --- SelectionSort — O(n^2) comparisons, at most n-1 swaps ------------------
template <typename T>
class SelectionSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "SelectionSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        const std::size_t n = d.size();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            std::size_t min = i;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (this->less(lt, d[j], d[min])) min = j;
            }
            if (min != i) this->swapAt(d, i, min);
        }
    }
};

// --- InsertionSort — O(n^2) worst, O(n) on nearly-sorted input --------------
template <typename T>
class InsertionSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "InsertionSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        const std::size_t n = d.size();
        for (std::size_t i = 1; i < n; ++i) {
            T key = std::move(d[i]);
            std::size_t j = i;
            while (j > 0 && this->less(lt, key, d[j - 1])) {
                d[j] = std::move(d[j - 1]);
                this->countMoves(1);
                --j;
            }
            d[j] = std::move(key);
            this->countMoves(1);
        }
    }
};

// --- MergeSort — stable O(n log n) with an auxiliary buffer -----------------
template <typename T>
class MergeSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "MergeSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        if (d.size() < 2) return;
        DynamicArray<T> aux = d;  // same size; roles alternate during merge
        msort(d, aux, lt, 0, d.size() - 1);
    }

private:
    void msort(DynamicArray<T>& d, DynamicArray<T>& aux, const Comparator& lt,
               std::size_t lo, std::size_t hi) {
        if (lo >= hi) return;
        const std::size_t mid = lo + (hi - lo) / 2;
        msort(d, aux, lt, lo, mid);
        msort(d, aux, lt, mid + 1, hi);
        merge(d, aux, lt, lo, mid, hi);
    }

    void merge(DynamicArray<T>& d, DynamicArray<T>& aux, const Comparator& lt,
               std::size_t lo, std::size_t mid, std::size_t hi) {
        for (std::size_t k = lo; k <= hi; ++k) aux[k] = d[k];
        this->countMoves(hi - lo + 1);
        std::size_t i = lo, j = mid + 1;
        for (std::size_t k = lo; k <= hi; ++k) {
            if (i > mid) {
                d[k] = std::move(aux[j++]);
            } else if (j > hi) {
                d[k] = std::move(aux[i++]);
            } else if (this->less(lt, aux[j], aux[i])) {
                d[k] = std::move(aux[j++]);
            } else {
                d[k] = std::move(aux[i++]);
            }
            this->countMoves(1);
        }
    }
};

// --- QuickSort — O(n log n) average; median-of-three guards sorted input ----
template <typename T>
class QuickSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "QuickSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        if (d.size() < 2) return;
        qsort(d, lt, 0, d.size() - 1);
    }

private:
    void qsort(DynamicArray<T>& d, const Comparator& lt, std::size_t lo,
               std::size_t hi) {
        while (lo < hi) {
            const std::size_t p = partition(d, lt, lo, hi);
            // Recurse into the smaller side, loop on the larger → O(log n) stack.
            if (p > lo && p - lo < hi - p) {
                if (p > 0) qsort(d, lt, lo, p - 1);
                lo = p + 1;
            } else {
                if (hi > p) qsort(d, lt, p + 1, hi);
                hi = (p > 0) ? p - 1 : 0;
                if (p == 0) break;
            }
        }
    }

    std::size_t partition(DynamicArray<T>& d, const Comparator& lt,
                          std::size_t lo, std::size_t hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        // Order lo/mid/hi so the median lands at hi as the pivot.
        if (this->less(lt, d[mid], d[lo])) this->swapAt(d, lo, mid);
        if (this->less(lt, d[hi], d[lo])) this->swapAt(d, lo, hi);
        if (this->less(lt, d[hi], d[mid])) this->swapAt(d, mid, hi);
        this->swapAt(d, mid, hi);

        std::size_t i = lo;
        for (std::size_t j = lo; j < hi; ++j) {
            if (this->less(lt, d[j], d[hi])) {
                this->swapAt(d, i, j);
                ++i;
            }
        }
        this->swapAt(d, i, hi);
        return i;
    }
};

// --- HeapSort — in-place O(n log n) via a max-heap over the array -----------
template <typename T>
class HeapSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    const char* name() const override { return "HeapSort"; }

    void sort(DynamicArray<T>& d, const Comparator& lt) override {
        this->resetCounters();
        const std::size_t n = d.size();
        if (n < 2) return;
        for (std::size_t i = n / 2; i-- > 0;) siftDown(d, lt, i, n);
        for (std::size_t end = n - 1; end > 0; --end) {
            this->swapAt(d, 0, end);
            siftDown(d, lt, 0, end);
        }
    }

private:
    void siftDown(DynamicArray<T>& d, const Comparator& lt, std::size_t i,
                  std::size_t n) {
        while (true) {
            std::size_t largest = i;
            const std::size_t l = 2 * i + 1, r = 2 * i + 2;
            if (l < n && this->less(lt, d[largest], d[l])) largest = l;
            if (r < n && this->less(lt, d[largest], d[r])) largest = r;
            if (largest == i) break;
            this->swapAt(d, i, largest);
            i = largest;
        }
    }
};

}  // namespace papertrade
