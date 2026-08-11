//
// test_graph_backtest.cpp — Phase-9 graph + Phase-10 DP backtest
// (spec §4 rows 12-14).
//
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/domain/Backtest.h"
#include "papertrade/structures/StockGraph.h"

using namespace papertrade;

namespace {
DynamicArray<double> series(const std::vector<double>& v) {
    DynamicArray<double> d;
    for (double x : v) d.push_back(x);
    return d;
}
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
}  // namespace

// --- StockGraph -------------------------------------------------------------
TEST_CASE("StockGraph tracks vertices and undirected edges", "[graph]") {
    StockGraph g;
    g.addEdge("AAPL", "MSFT", 0.2);
    g.addEdge("AAPL", "NVDA", 0.5);
    g.addVertex("TSLA");
    REQUIRE(g.vertexCount() == 4);
    REQUIRE(g.edgeCount() == 2);
    REQUIRE(g.hasVertex("TSLA"));

    auto nbrs = g.neighbors("AAPL");
    REQUIRE(nbrs.size() == 2);
    REQUIRE(contains(nbrs, "MSFT"));
    REQUIRE(contains(nbrs, "NVDA"));
    REQUIRE(contains(g.neighbors("MSFT"), "AAPL"));  // undirected
}

TEST_CASE("StockGraph BFS visits the connected component from a start", "[graph]") {
    StockGraph g;
    g.addEdge("A", "B", 1.0);
    g.addEdge("B", "C", 1.0);
    g.addEdge("A", "D", 1.0);
    g.addVertex("Z");  // isolated

    auto order = g.bfs("A");
    REQUIRE(order.size() == 4);
    REQUIRE(order.front() == "A");
    REQUIRE(contains(order, "B"));
    REQUIRE(contains(order, "C"));
    REQUIRE(contains(order, "D"));
    REQUIRE_FALSE(contains(order, "Z"));  // unreachable
    REQUIRE(g.bfs("nope").empty());        // unknown start
}

TEST_CASE("StockGraph Dijkstra finds shortest correlation distances", "[graph]") {
    StockGraph g;
    g.addEdge("A", "B", 1.0);
    g.addEdge("B", "C", 2.0);
    g.addEdge("A", "C", 4.0);  // direct A-C = 4, but A-B-C = 3
    g.addEdge("C", "D", 1.0);

    auto d = g.dijkstra("A");
    // Turn into an easy-to-assert lookup.
    auto distOf = [&](const std::string& t) {
        for (auto& pr : d)
            if (pr.first == t) return pr.second;
        return -1.0;
    };
    REQUIRE(distOf("A") == 0.0);
    REQUIRE(distOf("B") == 1.0);
    REQUIRE(distOf("C") == 3.0);  // via B, not the direct 4-weight edge
    REQUIRE(distOf("D") == 4.0);
}

TEST_CASE("StockGraph MST connects all vertices at minimum weight", "[graph]") {
    StockGraph g;
    g.addEdge("A", "B", 1.0);
    g.addEdge("B", "C", 2.0);
    g.addEdge("A", "C", 3.0);  // redundant, heavier — excluded by MST
    g.addEdge("C", "D", 4.0);

    auto mst = g.minimumSpanningTree();
    REQUIRE(mst.edges.size() == 3);        // n-1 edges for 4 vertices
    REQUIRE(mst.totalWeight == 7.0);       // 1 + 2 + 4, skipping the weight-3 edge
}

// --- Backtest (DP) ----------------------------------------------------------
TEST_CASE("Backtest bestSingle finds the best one buy/sell", "[backtest]") {
    auto t = Backtest::bestSingle(series({7, 1, 5, 3, 6, 4}));
    REQUIRE(t.profit() == 5.0);   // buy @1 (day 1), sell @6 (day 4)
    REQUIRE(t.buyDay == 1);
    REQUIRE(t.sellDay == 4);

    auto none = Backtest::bestSingle(series({7, 6, 4, 3, 1}));
    REQUIRE(none.profit() == 0.0);  // only falls → no trade
}

TEST_CASE("Backtest unlimited captures every up-run", "[backtest]") {
    auto r = Backtest::unlimited(series({7, 1, 5, 3, 6, 4}));
    REQUIRE(r.profit == 7.0);        // (5-1) + (6-3)
    REQUIRE(r.trades.size() == 2);

    auto mono = Backtest::unlimited(series({1, 2, 3, 4, 5}));
    REQUIRE(mono.profit == 4.0);
    REQUIRE(mono.trades.size() == 1);  // a single run

    REQUIRE(Backtest::unlimited(series({5, 4, 3, 2, 1})).profit == 0.0);
}

TEST_CASE("Backtest maxProfitAtMostK respects the transaction cap", "[backtest]") {
    auto p = series({3, 2, 6, 5, 0, 3});
    REQUIRE(Backtest::maxProfitAtMostK(p, 2) == 7.0);  // (6-2) + (3-0)
    REQUIRE(Backtest::maxProfitAtMostK(p, 1) == 4.0);  // best single run
    REQUIRE(Backtest::maxProfitAtMostK(p, 0) == 0.0);

    // k large enough → unlimited behaviour.
    auto q = series({1, 2, 3, 4, 5});
    REQUIRE(Backtest::maxProfitAtMostK(q, 10) == 4.0);
}
