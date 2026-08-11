#pragma once
//
// NonComparisonSorts.h — the two non-comparison Sorter<T> strategies (Week 13):
// CountingSort and RadixSort (LSD, base 256).
//
// These never call the comparator — they sort by an integer key extracted from
// each element, so `comparisons()` stays 0 while comparison sorts rack up
// O(n log n). That gap is the whole point of the Week-13 demo. Both handle
// negative keys by offsetting to the minimum. The comparator argument is
// accepted to satisfy the Sorter<T> interface but ignored: these always order
// ascending by key.
//
// Internal integer bookkeeping (histograms/keys) uses std::vector — it is not
// domain data; the element buffer itself stays a DynamicArray<T>.
//
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/structures/sorters/InstrumentedSorter.h"

namespace papertrade {

// --- CountingSort — O(n + range), stable ------------------------------------
template <typename T>
class CountingSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    using Key = std::function<long long(const T&)>;

    explicit CountingSort(Key key = [](const T& v) { return static_cast<long long>(v); })
        : key_(std::move(key)) {}

    const char* name() const override { return "CountingSort"; }

    void sort(DynamicArray<T>& d, const Comparator& /*ignored*/) override {
        this->resetCounters();
        const std::size_t n = d.size();
        if (n < 2) return;

        long long min = key_(d[0]), max = min;
        for (std::size_t i = 1; i < n; ++i) {
            const long long k = key_(d[i]);
            if (k < min) min = k;
            if (k > max) max = k;
        }
        const std::size_t range = static_cast<std::size_t>(max - min) + 1;

        std::vector<std::size_t> count(range, 0);
        for (std::size_t i = 0; i < n; ++i)
            ++count[static_cast<std::size_t>(key_(d[i]) - min)];
        for (std::size_t r = 1; r < range; ++r) count[r] += count[r - 1];

        DynamicArray<T> out = d;  // same size; filled stably from the right
        for (std::size_t i = n; i-- > 0;) {
            const std::size_t slot = static_cast<std::size_t>(key_(d[i]) - min);
            out[--count[slot]] = std::move(d[i]);
            this->countMoves(1);
        }
        for (std::size_t i = 0; i < n; ++i) d[i] = std::move(out[i]);
        this->countMoves(n);
    }

private:
    Key key_;
};

// --- RadixSort — LSD, base 256, O(w·(n + 256)) ------------------------------
template <typename T>
class RadixSort : public InstrumentedSorter<T> {
public:
    using Comparator = typename InstrumentedSorter<T>::Comparator;
    using Key = std::function<long long(const T&)>;

    explicit RadixSort(Key key = [](const T& v) { return static_cast<long long>(v); })
        : key_(std::move(key)) {}

    const char* name() const override { return "RadixSort"; }

    void sort(DynamicArray<T>& d, const Comparator& /*ignored*/) override {
        this->resetCounters();
        const std::size_t n = d.size();
        if (n < 2) return;

        long long min = key_(d[0]);
        for (std::size_t i = 1; i < n; ++i) min = std::min(min, key_(d[i]));

        std::vector<std::uint64_t> keys(n);
        std::uint64_t maxKey = 0;
        for (std::size_t i = 0; i < n; ++i) {
            keys[i] = static_cast<std::uint64_t>(key_(d[i]) - min);
            maxKey = std::max(maxKey, keys[i]);
        }

        DynamicArray<T> buffer = d;              // ping-pong element storage
        std::vector<std::uint64_t> bufKeys(n);   // parallel keys
        DynamicArray<T>* src = &d;
        DynamicArray<T>* dst = &buffer;
        std::vector<std::uint64_t>* srcK = &keys;
        std::vector<std::uint64_t>* dstK = &bufKeys;

        for (unsigned shift = 0; (maxKey >> shift) > 0; shift += 8) {
            std::size_t count[256] = {0};
            for (std::size_t i = 0; i < n; ++i)
                ++count[((*srcK)[i] >> shift) & 0xFFu];
            for (int b = 1; b < 256; ++b) count[b] += count[b - 1];
            for (std::size_t i = n; i-- > 0;) {
                const std::size_t b = ((*srcK)[i] >> shift) & 0xFFu;
                const std::size_t pos = --count[b];
                (*dst)[pos] = std::move((*src)[i]);
                (*dstK)[pos] = (*srcK)[i];
                this->countMoves(1);
            }
            std::swap(src, dst);
            std::swap(srcK, dstK);
        }

        if (src != &d) {  // sorted data ended up in the buffer → copy back
            for (std::size_t i = 0; i < n; ++i) d[i] = std::move((*src)[i]);
            this->countMoves(n);
        }
    }

private:
    Key key_;
};

}  // namespace papertrade
