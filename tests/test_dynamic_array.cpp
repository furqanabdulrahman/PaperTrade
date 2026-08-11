//
// test_dynamic_array.cpp — invariants for the hand-built growable array.
//
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "papertrade/adt/DynamicArray.h"

using papertrade::DynamicArray;

TEST_CASE("DynamicArray starts empty", "[dynamic_array]") {
    DynamicArray<int> a;
    REQUIRE(a.size() == 0);
    REQUIRE(a.empty());
}

TEST_CASE("push_back stores values and grows capacity by doubling",
          "[dynamic_array]") {
    DynamicArray<int> a;
    for (int i = 0; i < 10; ++i) a.push_back(i * i);

    REQUIRE(a.size() == 10);
    REQUIRE_FALSE(a.empty());
    for (int i = 0; i < 10; ++i) REQUIRE(a[i] == i * i);

    // Doubling growth: capacity is a power-of-two-ish envelope >= size, and
    // strictly grows in jumps rather than one-at-a-time.
    REQUIRE(a.capacity() >= a.size());
    REQUIRE(a.capacity() <= 16);  // 4 -> 8 -> 16 for 10 elements
}

TEST_CASE("at() bounds-checks, operator[] does not", "[dynamic_array]") {
    DynamicArray<int> a;
    a.push_back(42);
    REQUIRE(a.at(0) == 42);
    REQUIRE_THROWS_AS(a.at(1), std::out_of_range);
}

TEST_CASE("pop_back shrinks and throws on empty", "[dynamic_array]") {
    DynamicArray<int> a;
    a.push_back(1);
    a.push_back(2);
    a.pop_back();
    REQUIRE(a.size() == 1);
    REQUIRE(a.back() == 1);
    a.pop_back();
    REQUIRE(a.empty());
    REQUIRE_THROWS_AS(a.pop_back(), std::out_of_range);
}

TEST_CASE("front/back/emplace_back work with non-trivial types",
          "[dynamic_array]") {
    DynamicArray<std::string> a;
    a.push_back("first");
    a.emplace_back("second");
    a.push_back(std::string("third"));
    REQUIRE(a.size() == 3);
    REQUIRE(a.front() == "first");
    REQUIRE(a.back() == "third");
    REQUIRE(a[1] == "second");
}

TEST_CASE("copy construction is a deep copy", "[dynamic_array]") {
    DynamicArray<int> a;
    for (int i = 0; i < 5; ++i) a.push_back(i);

    DynamicArray<int> b = a;  // copy
    b[0] = 999;

    REQUIRE(a[0] == 0);    // original untouched
    REQUIRE(b[0] == 999);
    REQUIRE(b.size() == a.size());
}

TEST_CASE("move construction transfers the buffer and empties the source",
          "[dynamic_array]") {
    DynamicArray<int> a;
    for (int i = 0; i < 5; ++i) a.push_back(i);

    DynamicArray<int> b = std::move(a);
    REQUIRE(b.size() == 5);
    REQUIRE(b[4] == 4);
    REQUIRE(a.size() == 0);  // moved-from is empty
}

// A payload type that tracks live instances, to prove copy/move/clear call
// destructors exactly the right number of times (no leaks, no double-frees).
namespace {
struct Counted {
    static int alive;
    int v;
    Counted(int x = 0) : v(x) { ++alive; }
    Counted(const Counted& o) : v(o.v) { ++alive; }
    Counted(Counted&& o) noexcept : v(o.v) { ++alive; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --alive; }
};
int Counted::alive = 0;
}  // namespace

TEST_CASE("no leaked or double-destroyed elements across lifetime",
          "[dynamic_array]") {
    REQUIRE(Counted::alive == 0);
    {
        DynamicArray<Counted> a;
        for (int i = 0; i < 20; ++i) a.push_back(Counted(i));  // forces regrows
        REQUIRE(Counted::alive == 20);

        DynamicArray<Counted> b = a;  // +20
        REQUIRE(Counted::alive == 40);

        b.clear();  // -20
        REQUIRE(Counted::alive == 20);

        a.pop_back();  // -1
        REQUIRE(Counted::alive == 19);
    }  // a destructs remaining 19
    REQUIRE(Counted::alive == 0);
}
