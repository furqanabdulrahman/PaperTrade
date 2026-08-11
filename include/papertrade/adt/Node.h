#pragma once
//
// Node.h — common base for every linked node used across lists, trees, and the
// graph (spec §4 row 5: "Node base + unique_ptr ownership throughout #2–8").
//
// The base owns the payload and provides a virtual destructor so derived nodes
// (singly/doubly/circular list nodes, tree nodes, graph vertices) can be owned
// and destroyed polymorphically. Concrete nodes add their own unique_ptr links,
// which is where the RAII memory-safety guarantee actually lives: destroying the
// head of a chain recursively frees the whole chain with no manual `delete`.
//
#include <utility>

namespace papertrade {

template <typename T>
struct Node {
    T value;

    explicit Node(const T& v) : value(v) {}
    explicit Node(T&& v) : value(std::move(v)) {}

    virtual ~Node() = default;
};

}  // namespace papertrade
