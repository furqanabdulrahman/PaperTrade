#pragma once
//
// StockGraph.h — weighted correlation graph (spec §4 rows 12-13).
//
// Adjacency-list graph over tickers; edges carry a weight of (1 − correlation),
// so strongly-correlated stocks sit "close". Vertices are addressed by ticker
// string (hashed to a dense integer id). Provides the three graded algorithms:
//   * bfs        — related-stock discovery, using our OrderQueue (FIFO frontier)
//   * dijkstra   — correlation distance, using our MinHeap as the priority queue
//   * minimumSpanningTree — diversification backbone, via Kruskal + union-find
//
// Storage uses our own DynamicArray / hash table; only algorithm return values
// use std::vector.
//
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/Graph.h"
#include "papertrade/structures/MinHeap.h"
#include "papertrade/structures/OrderQueue.h"
#include "papertrade/structures/SeparateChainingHashTable.h"

namespace papertrade {

class StockGraph : public Graph {
public:
    struct MstEdge {
        std::string a, b;
        double weight;
    };
    struct Mst {
        double totalWeight = 0.0;
        std::vector<MstEdge> edges;
    };

    void addVertex(const std::string& id) override {
        if (index_.contains(id)) return;
        const int newId = static_cast<int>(labels_.size());
        index_.put(id, newId);
        labels_.push_back(id);
        adj_.push_back(DynamicArray<Edge>());
    }

    void addEdge(const std::string& a, const std::string& b, double weight) override {
        addVertex(a);
        addVertex(b);
        const int ia = *index_.get(a), ib = *index_.get(b);
        adj_[ia].push_back({ib, weight});
        adj_[ib].push_back({ia, weight});  // undirected
        ++edgeCount_;
    }

    bool hasVertex(const std::string& id) const override { return index_.contains(id); }
    std::size_t vertexCount() const override { return labels_.size(); }
    std::size_t edgeCount() const override { return edgeCount_; }

    // Immediate neighbours of a ticker (empty if unknown).
    std::vector<std::string> neighbors(const std::string& id) const {
        std::vector<std::string> out;
        const int* v = index_.get(id);
        if (!v) return out;
        for (std::size_t i = 0; i < adj_[*v].size(); ++i)
            out.push_back(labels_[adj_[*v][i].to]);
        return out;
    }

    // Breadth-first order from `start` (related-stock discovery).
    std::vector<std::string> bfs(const std::string& start) const {
        std::vector<std::string> order;
        const int* s = index_.get(start);
        if (!s) return order;

        std::vector<bool> seen(labels_.size(), false);
        OrderQueue<int> frontier;
        frontier.enqueue(*s);
        seen[*s] = true;
        while (!frontier.empty()) {
            const int v = frontier.front();
            frontier.dequeue();
            order.push_back(labels_[v]);
            for (std::size_t i = 0; i < adj_[v].size(); ++i) {
                const int to = adj_[v][i].to;
                if (!seen[to]) {
                    seen[to] = true;
                    frontier.enqueue(to);
                }
            }
        }
        return order;
    }

    // Shortest correlation distance from `start` to every reachable ticker,
    // computed with our MinHeap as the priority queue. Returned in id order.
    std::vector<std::pair<std::string, double>> dijkstra(const std::string& start) const {
        std::vector<std::pair<std::string, double>> out;
        const int* s = index_.get(start);
        if (!s) return out;

        const double kInf = std::numeric_limits<double>::infinity();
        std::vector<double> dist(labels_.size(), kInf);
        std::vector<bool> done(labels_.size(), false);
        dist[*s] = 0.0;

        MinHeap<PqNode> pq([](const PqNode& a, const PqNode& b) { return a.dist < b.dist; });
        pq.push({*s, 0.0});
        while (!pq.empty()) {
            const PqNode cur = pq.pop();
            if (done[cur.v]) continue;
            done[cur.v] = true;
            for (std::size_t i = 0; i < adj_[cur.v].size(); ++i) {
                const Edge& e = adj_[cur.v][i];
                if (dist[cur.v] + e.weight < dist[e.to]) {
                    dist[e.to] = dist[cur.v] + e.weight;
                    pq.push({e.to, dist[e.to]});
                }
            }
        }
        for (std::size_t i = 0; i < labels_.size(); ++i)
            if (dist[i] != kInf) out.push_back({labels_[i], dist[i]});
        return out;
    }

    // Minimum spanning tree (Kruskal + union-find): the cheapest set of edges
    // that connects the graph — the diversification backbone.
    Mst minimumSpanningTree() const {
        std::vector<KEdge> edges;
        for (std::size_t v = 0; v < adj_.size(); ++v)
            for (std::size_t i = 0; i < adj_[v].size(); ++i)
                if (adj_[v][i].to > static_cast<int>(v))  // each undirected edge once
                    edges.push_back({static_cast<int>(v), adj_[v][i].to, adj_[v][i].weight});
        std::sort(edges.begin(), edges.end(),
                  [](const KEdge& x, const KEdge& y) { return x.weight < y.weight; });

        std::vector<int> parent(labels_.size()), rank(labels_.size(), 0);
        for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);

        Mst result;
        for (const KEdge& e : edges) {
            const int ra = find(parent, e.a), rb = find(parent, e.b);
            if (ra == rb) continue;  // would form a cycle
            if (rank[ra] < rank[rb]) {
                parent[ra] = rb;
            } else if (rank[ra] > rank[rb]) {
                parent[rb] = ra;
            } else {
                parent[rb] = ra;
                ++rank[ra];
            }
            result.edges.push_back({labels_[e.a], labels_[e.b], e.weight});
            result.totalWeight += e.weight;
        }
        return result;
    }

private:
    struct Edge {
        int to;
        double weight;
    };
    struct PqNode {
        int v;
        double dist;
    };
    struct KEdge {
        int a, b;
        double weight;
    };

    static int find(std::vector<int>& parent, int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];  // path halving
            x = parent[x];
        }
        return x;
    }

    SeparateChainingHashTable<std::string, int> index_;  // ticker -> dense id
    DynamicArray<std::string> labels_;                   // id -> ticker
    DynamicArray<DynamicArray<Edge>> adj_;
    std::size_t edgeCount_ = 0;
};

}  // namespace papertrade
