//
// test_heaps.cpp — invariants for Phase-6 heaps (spec §4 row 9).
//
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "papertrade/adt/Heap.h"
#include "papertrade/structures/MaxHeap.h"
#include "papertrade/structures/MinHeap.h"

using namespace papertrade;

TEST_CASE("MinHeap pops in ascending order", "[minheap]") {
    MinHeap<int> h;
    REQUIRE(h.empty());
    for (int v : {5, 1, 8, 3, 9, 2, 7}) h.push(v);
    REQUIRE(h.size() == 7);
    REQUIRE(h.peek() == 1);

    std::vector<int> out;
    while (!h.empty()) out.push_back(h.pop());
    REQUIRE(out == std::vector<int>{1, 2, 3, 5, 7, 8, 9});
    REQUIRE(h.empty());
}

TEST_CASE("MaxHeap pops in descending order", "[maxheap]") {
    MaxHeap<int> h;
    for (int v : {5, 1, 8, 3, 9, 2, 7}) h.push(v);
    REQUIRE(h.peek() == 9);

    std::vector<int> out;
    while (!h.empty()) out.push_back(h.pop());
    REQUIRE(out == std::vector<int>{9, 8, 7, 5, 3, 2, 1});
}

TEST_CASE("Heap peek/pop on empty throws", "[heap]") {
    MinHeap<int> h;
    REQUIRE_THROWS(h.peek());
    REQUIRE_THROWS(h.pop());
    h.push(42);
    REQUIRE(h.pop() == 42);
    REQUIRE_THROWS(h.pop());
}

TEST_CASE("Heap clear empties without disturbing later use", "[heap]") {
    MaxHeap<int> h;
    for (int v : {3, 1, 2}) h.push(v);
    h.clear();
    REQUIRE(h.empty());
    REQUIRE(h.size() == 0);
    h.push(10);
    h.push(20);
    REQUIRE(h.pop() == 20);
}

namespace {
struct Mover {
    std::string sym;
    double pct;
};
}  // namespace

TEST_CASE("MaxHeap with a custom comparator drives Top-Gainers", "[maxheap]") {
    // Heap movers by day % change; the biggest gainer sits at the root.
    MaxHeap<Mover> gainers([](const Mover& a, const Mover& b) { return a.pct < b.pct; });
    gainers.push({"AAPL", 1.2});
    gainers.push({"NVDA", 7.5});
    gainers.push({"MSFT", -0.4});
    gainers.push({"TSLA", 4.1});

    REQUIRE(gainers.peek().sym == "NVDA");
    // Top-2 gainers via two O(log n) pops — no full sort needed.
    REQUIRE(gainers.pop().sym == "NVDA");
    REQUIRE(gainers.pop().sym == "TSLA");
}

TEST_CASE("MinHeap with a custom comparator drives Top-Losers", "[minheap]") {
    MinHeap<Mover> losers([](const Mover& a, const Mover& b) { return a.pct < b.pct; });
    losers.push({"AAPL", 1.2});
    losers.push({"NVDA", 7.5});
    losers.push({"MSFT", -3.4});
    losers.push({"TSLA", -0.4});
    REQUIRE(losers.peek().sym == "MSFT");  // worst performer on top
    REQUIRE(losers.pop().sym == "MSFT");
    REQUIRE(losers.pop().sym == "TSLA");
}

TEST_CASE("Heaps are usable through a Heap<T> base pointer", "[heap][polymorphism]") {
    MinHeap<int> mn;
    MaxHeap<int> mx;
    Heap<int>* heaps[] = {&mn, &mx};
    for (Heap<int>* h : heaps)
        for (int v : {4, 2, 6}) h->push(v);

    REQUIRE(heaps[0]->peek() == 2);  // MinHeap root
    REQUIRE(heaps[1]->peek() == 6);  // MaxHeap root — same interface, different order
}

TEST_CASE("Heapsort property: repeated pop yields fully sorted output", "[heap]") {
    MinHeap<int> h;
    // Reverse-sorted input is the worst case for many sorts; the heap is immune.
    for (int v = 50; v >= 1; --v) h.push(v);
    bool sorted = true;
    int prev = h.pop();
    while (!h.empty()) {
        int cur = h.pop();
        if (cur < prev) sorted = false;
        prev = cur;
    }
    REQUIRE(sorted);
}
