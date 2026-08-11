#pragma once
//
// Graph.h — abstract weighted-graph ADT (spec §4 rows 12–13).
//
// StockGraph (Phase 9) implements this as an adjacency list over the ~100-ticker
// correlation graph and adds BFS (related stocks), Dijkstra (correlation
// distance) and an MST (diversification clusters). Vertices are addressed by
// ticker string; edges carry a weight of (1 − correlation).
//
#include <cstddef>
#include <string>

namespace papertrade {

class Graph {
public:
    virtual ~Graph() = default;

    virtual void addVertex(const std::string& id) = 0;
    virtual void addEdge(const std::string& a, const std::string& b,
                         double weight) = 0;
    virtual bool hasVertex(const std::string& id) const = 0;

    virtual std::size_t vertexCount() const = 0;
    virtual std::size_t edgeCount() const = 0;
};

}  // namespace papertrade
