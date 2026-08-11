//
// test_hashtable_orders.cpp — Phase-8 hash table + order engine (spec §4 row 11).
//
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "papertrade/domain/AccountBook.h"
#include "papertrade/domain/Portfolio.h"
#include "papertrade/structures/SeparateChainingHashTable.h"

using namespace papertrade;

// --- SeparateChainingHashTable ----------------------------------------------
TEST_CASE("HashTable put/get/contains/remove basics", "[hashtable]") {
    SeparateChainingHashTable<std::string, int> h;
    REQUIRE(h.empty());
    h.put("AAPL", 1);
    h.put("MSFT", 2);
    h.put("NVDA", 3);
    REQUIRE(h.size() == 3);
    REQUIRE(*h.get("MSFT") == 2);
    REQUIRE(h.contains("NVDA"));
    REQUIRE(h.get("TSLA") == nullptr);

    h.put("MSFT", 20);  // update existing, not a new entry
    REQUIRE(h.size() == 3);
    REQUIRE(*h.get("MSFT") == 20);

    REQUIRE(h.remove("MSFT"));
    REQUIRE_FALSE(h.contains("MSFT"));
    REQUIRE(h.size() == 2);
    REQUIRE_FALSE(h.remove("MSFT"));  // already gone
}

TEST_CASE("HashTable grows buckets and keeps all entries", "[hashtable]") {
    SeparateChainingHashTable<int, int> h(8);
    const std::size_t before = h.bucketCount();
    for (int i = 0; i < 200; ++i) h.put(i, i * i);
    REQUIRE(h.size() == 200);
    REQUIRE(h.bucketCount() > before);  // resized under load
    for (int i = 0; i < 200; ++i) {
        REQUIRE(h.get(i) != nullptr);
        REQUIRE(*h.get(i) == i * i);
    }
}

TEST_CASE("HashTable forEach visits every entry once", "[hashtable]") {
    SeparateChainingHashTable<int, int> h;
    long expected = 0;
    for (int i = 1; i <= 50; ++i) {
        h.put(i, i);
        expected += i;
    }
    long sum = 0;
    std::size_t count = 0;
    h.forEach([&](const int&, const int& v) {
        sum += v;
        ++count;
    });
    REQUIRE(count == 50);
    REQUIRE(sum == expected);
}

TEST_CASE("HashTable getOrCreate default-constructs on miss", "[hashtable]") {
    SeparateChainingHashTable<std::string, int> h;
    int& v = h.getOrCreate("x");
    REQUIRE(v == 0);
    v = 99;
    REQUIRE(*h.get("x") == 99);
    REQUIRE(h.getOrCreate("x") == 99);  // existing returned, not reset
    REQUIRE(h.size() == 1);
}

// --- Portfolio order engine -------------------------------------------------
TEST_CASE("Portfolio buy debits cash and averages cost", "[portfolio]") {
    Portfolio p(10000.0);
    REQUIRE(p.buy("AAPL", 10, 100.0).ok);   // spend 1000
    REQUIRE(p.cash() == 9000.0);
    REQUIRE(p.position("AAPL")->qty == 10);
    REQUIRE(p.position("AAPL")->avgCost == 100.0);

    REQUIRE(p.buy("AAPL", 10, 120.0).ok);   // spend 1200; avg (1000+1200)/20 = 110
    REQUIRE(p.position("AAPL")->qty == 20);
    REQUIRE(p.position("AAPL")->avgCost == 110.0);
    REQUIRE(p.cash() == 7800.0);
    REQUIRE(p.orderCount() == 2);
}

TEST_CASE("Portfolio rejects overspending and overselling", "[portfolio]") {
    Portfolio p(1000.0);
    auto r = p.buy("AAPL", 100, 50.0);  // needs 5000
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.message == "insufficient cash");
    REQUIRE(p.cash() == 1000.0);

    p.buy("AAPL", 10, 50.0);  // own 10
    auto s = p.sell("AAPL", 20, 60.0);
    REQUIRE_FALSE(s.ok);
    REQUIRE(s.message == "insufficient shares");
}

TEST_CASE("Portfolio sell realises proceeds and clears a zeroed position",
          "[portfolio]") {
    Portfolio p(10000.0);
    p.buy("NVDA", 5, 100.0);       // cash 9500
    REQUIRE(p.sell("NVDA", 5, 130.0).ok);
    REQUIRE(p.cash() == 10150.0);  // 9500 + 650
    REQUIRE(p.position("NVDA") == nullptr);  // fully sold → removed
}

TEST_CASE("Portfolio undo restores exact pre-trade state", "[portfolio]") {
    Portfolio p(10000.0);
    p.buy("AAPL", 10, 100.0);      // cash 9000, qty 10 @100
    p.buy("AAPL", 10, 140.0);      // cash 7600, qty 20 @120

    REQUIRE(p.undoLast());          // reverse the second buy
    REQUIRE(p.cash() == 9000.0);
    REQUIRE(p.position("AAPL")->qty == 10);
    REQUIRE(p.position("AAPL")->avgCost == 100.0);
    REQUIRE(p.orderCount() == 1);

    REQUIRE(p.undoLast());          // reverse the first buy → back to start
    REQUIRE(p.cash() == 10000.0);
    REQUIRE(p.position("AAPL") == nullptr);
    REQUIRE_FALSE(p.undoLast());     // nothing left
}

TEST_CASE("Portfolio marketValue prices holdings via a lookup", "[portfolio]") {
    Portfolio p(10000.0);
    p.buy("AAPL", 10, 100.0);  // cash 9000, 10 shares
    p.buy("MSFT", 5, 200.0);   // cash 8000, 5 shares
    const double mv = p.marketValue([](const std::string& t) {
        return t == "AAPL" ? 150.0 : 250.0;
    });
    // 8000 cash + 10*150 + 5*250 = 8000 + 1500 + 1250
    REQUIRE(mv == 10750.0);
}

// --- AccountBook multi-tenancy ----------------------------------------------
TEST_CASE("AccountBook isolates portfolios per user", "[accounts]") {
    AccountBook book;
    REQUIRE(book.userCount() == 0);

    book.portfolio("alice").buy("AAPL", 10, 100.0);
    book.portfolio("bob").buy("NVDA", 1, 500.0);

    REQUIRE(book.userCount() == 2);
    REQUIRE(book.exists("alice"));
    REQUIRE(book.portfolio("alice").position("AAPL")->qty == 10);
    REQUIRE(book.portfolio("alice").position("NVDA") == nullptr);  // bob's, not alice's
    REQUIRE(book.portfolio("bob").position("NVDA")->qty == 1);
    REQUIRE(book.portfolio("alice").cash() == 99000.0);
    REQUIRE(book.portfolio("bob").cash() == 99500.0);
}
