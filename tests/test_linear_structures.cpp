//
// test_linear_structures.cpp — invariants for Phase-3 linked/LIFO/FIFO types.
//
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "papertrade/structures/CircularLinkedList.h"
#include "papertrade/structures/DoublyLinkedList.h"
#include "papertrade/structures/OrderQueue.h"
#include "papertrade/structures/OrderStack.h"
#include "papertrade/structures/PriceRefreshCycle.h"
#include "papertrade/structures/RecentlyViewed.h"
#include "papertrade/structures/SinglyLinkedList.h"

using namespace papertrade;

// --- SinglyLinkedList -------------------------------------------------------
TEST_CASE("SinglyLinkedList push/pop and ordering", "[singly]") {
    SinglyLinkedList<int> l;
    REQUIRE(l.empty());

    l.pushBack(1);
    l.pushBack(2);
    l.pushBack(3);
    l.pushFront(0);  // 0,1,2,3
    REQUIRE(l.size() == 4);
    REQUIRE(l.front() == 0);

    std::vector<int> seen;
    l.forEach([&](int v) { seen.push_back(v); });
    REQUIRE(seen == std::vector<int>{0, 1, 2, 3});

    REQUIRE(l.popFront());
    REQUIRE(l.front() == 1);
    REQUIRE(l.size() == 3);

    l.clear();
    REQUIRE(l.empty());
    REQUIRE_FALSE(l.popFront());  // pop on empty is false, not UB
}

TEST_CASE("SinglyLinkedList tail stays valid across pop to empty then push",
          "[singly]") {
    SinglyLinkedList<int> l;
    l.pushBack(1);
    REQUIRE(l.popFront());  // now empty, tail must be reset
    l.pushBack(9);          // must not dereference a dangling tail
    l.pushBack(10);
    REQUIRE(l.front() == 9);
    REQUIRE(l.size() == 2);
}

// --- DoublyLinkedList -------------------------------------------------------
TEST_CASE("DoublyLinkedList both-end ops and reverse traversal", "[doubly]") {
    DoublyLinkedList<int> d;
    d.pushBack(2);
    d.pushBack(3);
    d.pushFront(1);  // 1,2,3
    REQUIRE(d.front() == 1);
    REQUIRE(d.back() == 3);

    std::vector<int> fwd, rev;
    d.forEach([&](int v) { fwd.push_back(v); });
    d.forEachReverse([&](int v) { rev.push_back(v); });
    REQUIRE(fwd == std::vector<int>{1, 2, 3});
    REQUIRE(rev == std::vector<int>{3, 2, 1});

    REQUIRE(d.popBack());
    REQUIRE(d.back() == 2);
    REQUIRE(d.popBack());
    REQUIRE(d.popBack());
    REQUIRE(d.empty());
    REQUIRE_FALSE(d.popBack());
}

// --- RecentlyViewed ---------------------------------------------------------
TEST_CASE("RecentlyViewed back/forward navigation", "[recent]") {
    RecentlyViewed rv;
    rv.visit("AAPL");
    rv.visit("MSFT");
    rv.visit("NVDA");
    REQUIRE(*rv.current() == "NVDA");
    REQUIRE(rv.canBack());
    REQUIRE_FALSE(rv.canForward());

    REQUIRE(*rv.back() == "MSFT");
    REQUIRE(*rv.back() == "AAPL");
    REQUIRE_FALSE(rv.canBack());
    REQUIRE(rv.canForward());

    REQUIRE(*rv.forward() == "MSFT");
    REQUIRE(*rv.current() == "MSFT");
}

TEST_CASE("RecentlyViewed visiting after back truncates forward history",
          "[recent]") {
    RecentlyViewed rv;
    rv.visit("AAPL");
    rv.visit("MSFT");
    rv.visit("NVDA");
    rv.back();         // at MSFT, forward = {NVDA}
    rv.visit("TSLA");  // should discard NVDA, append TSLA after MSFT

    REQUIRE(*rv.current() == "TSLA");
    REQUIRE_FALSE(rv.canForward());
    REQUIRE(*rv.back() == "MSFT");
    REQUIRE(*rv.back() == "AAPL");
    REQUIRE(rv.size() == 3);  // history is exactly AAPL, MSFT, TSLA
}

TEST_CASE("RecentlyViewed ignores re-visiting the current ticker", "[recent]") {
    RecentlyViewed rv;
    rv.visit("AAPL");
    rv.visit("AAPL");
    REQUIRE(rv.size() == 1);
}

// --- CircularLinkedList / PriceRefreshCycle ---------------------------------
TEST_CASE("CircularLinkedList advance cycles forever", "[circular]") {
    CircularLinkedList<int> c;
    c.append(10);
    c.append(20);
    c.append(30);

    std::vector<int> order;
    for (int i = 0; i < 7; ++i) order.push_back(c.advance());
    REQUIRE(order == std::vector<int>{10, 20, 30, 10, 20, 30, 10});
}

TEST_CASE("PriceRefreshCycle round-robins the universe", "[refresh-cycle]") {
    PriceRefreshCycle cycle;
    cycle.addTicker("AAPL");
    cycle.addTicker("MSFT");
    REQUIRE(cycle.size() == 2);

    REQUIRE(cycle.nextTicker() == "AAPL");
    REQUIRE(cycle.nextTicker() == "MSFT");
    REQUIRE(cycle.nextTicker() == "AAPL");  // wrapped
}

// --- OrderStack (LIFO) ------------------------------------------------------
TEST_CASE("OrderStack is LIFO", "[order-stack]") {
    OrderStack<std::string> s;
    REQUIRE(s.empty());
    s.push("buy-1");
    s.push("buy-2");
    s.push("sell-1");
    REQUIRE(s.size() == 3);
    REQUIRE(s.top() == "sell-1");

    REQUIRE(s.pop());
    REQUIRE(s.top() == "buy-2");  // undo pops most-recent first
    REQUIRE(s.pop());
    REQUIRE(s.pop());
    REQUIRE(s.empty());
    REQUIRE_FALSE(s.pop());
    REQUIRE_THROWS(s.top());
}

// --- OrderQueue (FIFO) ------------------------------------------------------
TEST_CASE("OrderQueue is FIFO", "[order-queue]") {
    OrderQueue<int> q;
    REQUIRE(q.empty());
    q.enqueue(1);
    q.enqueue(2);
    q.enqueue(3);
    REQUIRE(q.front() == 1);  // processed in submission order

    REQUIRE(q.dequeue());
    REQUIRE(q.front() == 2);
    REQUIRE(q.dequeue());
    REQUIRE(q.dequeue());
    REQUIRE(q.empty());
    REQUIRE_FALSE(q.dequeue());
    REQUIRE_THROWS(q.front());
}
