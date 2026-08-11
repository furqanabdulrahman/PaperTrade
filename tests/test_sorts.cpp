//
// test_sorts.cpp — invariants for Phase-7 sorters (spec §4 row 10).
//
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/Sorter.h"
#include "papertrade/structures/sorters/ComparisonSorts.h"
#include "papertrade/structures/sorters/NonComparisonSorts.h"

using namespace papertrade;

namespace {

DynamicArray<int> toArray(const std::vector<int>& v) {
    DynamicArray<int> d;
    for (int x : v) d.push_back(x);
    return d;
}

std::vector<int> toVector(const DynamicArray<int>& d) {
    std::vector<int> v;
    for (std::size_t i = 0; i < d.size(); ++i) v.push_back(d[i]);
    return v;
}

const auto ascending = [](const int& a, const int& b) { return a < b; };

// Every sorter must reproduce std::sort's result on the same input.
void checkSorts(Sorter<int>& sorter, std::vector<int> input) {
    std::vector<int> expected = input;
    std::sort(expected.begin(), expected.end());

    DynamicArray<int> d = toArray(input);
    sorter.sort(d, ascending);
    INFO("algorithm = " << sorter.name());
    REQUIRE(toVector(d) == expected);
}

}  // namespace

TEST_CASE("Every sorter sorts across representative inputs", "[sort]") {
    std::vector<std::unique_ptr<Sorter<int>>> sorters;
    sorters.push_back(std::make_unique<BubbleSort<int>>());
    sorters.push_back(std::make_unique<SelectionSort<int>>());
    sorters.push_back(std::make_unique<InsertionSort<int>>());
    sorters.push_back(std::make_unique<MergeSort<int>>());
    sorters.push_back(std::make_unique<QuickSort<int>>());
    sorters.push_back(std::make_unique<HeapSort<int>>());
    sorters.push_back(std::make_unique<CountingSort<int>>());
    sorters.push_back(std::make_unique<RadixSort<int>>());

    const std::vector<std::vector<int>> inputs = {
        {},
        {42},
        {2, 1},
        {5, 3, 8, 1, 9, 2, 7, 4, 6, 0},
        {1, 2, 3, 4, 5, 6, 7},           // already sorted
        {7, 6, 5, 4, 3, 2, 1},           // reverse sorted
        {4, 4, 2, 2, 8, 8, 1, 1},        // duplicates
        {-5, 3, -1, 0, -8, 7, 2, -3},    // negatives (offset handling)
    };

    for (auto& sorter : sorters) {
        for (const auto& in : inputs) checkSorts(*sorter, in);
    }
}

TEST_CASE("Sorters are selected polymorphically via Sorter<T>* (Strategy)",
          "[sort][polymorphism]") {
    std::vector<int> data = {3, 1, 2};
    std::unique_ptr<Sorter<int>> strategy = std::make_unique<QuickSort<int>>();
    DynamicArray<int> d = toArray(data);
    strategy->sort(d, ascending);
    REQUIRE(toVector(d) == std::vector<int>{1, 2, 3});
    REQUIRE(std::string(strategy->name()) == "QuickSort");

    strategy = std::make_unique<RadixSort<int>>();  // swap algorithm at runtime
    DynamicArray<int> d2 = toArray(data);
    strategy->sort(d2, ascending);
    REQUIRE(toVector(d2) == std::vector<int>{1, 2, 3});
}

TEST_CASE("Instrumentation resets per call and reflects algorithmic cost",
          "[sort][instrumentation]") {
    std::mt19937 rng(7);
    std::vector<int> big(500);
    for (int& x : big) x = static_cast<int>(rng() % 10000);

    BubbleSort<int> bubble;
    MergeSort<int> merge;
    DynamicArray<int> a = toArray(big), b = toArray(big);
    bubble.sort(a, ascending);
    merge.sort(b, ascending);

    // O(n^2) bubble makes far more comparisons than O(n log n) merge.
    REQUIRE(bubble.comparisons() > merge.comparisons());
    REQUIRE(merge.comparisons() > 0);

    // Counters reset each call: sorting an already-sorted array is cheaper.
    bubble.sort(a, ascending);  // `a` is now sorted → single early-exit pass
    REQUIRE(bubble.comparisons() < 500 * 500);
}

TEST_CASE("Non-comparison sorts perform zero comparisons", "[sort][noncompare]") {
    std::vector<int> data = {5, 3, 8, 1, 9, 2, 7};
    CountingSort<int> counting;
    RadixSort<int> radix;

    DynamicArray<int> a = toArray(data), b = toArray(data);
    counting.sort(a, ascending);
    radix.sort(b, ascending);

    REQUIRE(counting.comparisons() == 0);
    REQUIRE(radix.comparisons() == 0);
    REQUIRE(counting.moves() > 0);
    REQUIRE(radix.moves() > 0);
}

TEST_CASE("Sorters honour a custom comparator (descending)", "[sort]") {
    const auto descending = [](const int& a, const int& b) { return a > b; };
    HeapSort<int> heap;
    DynamicArray<int> d = toArray({1, 5, 3, 2, 4});
    heap.sort(d, descending);
    REQUIRE(toVector(d) == std::vector<int>{5, 4, 3, 2, 1});
}
