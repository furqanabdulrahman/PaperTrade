#pragma once
//
// BST.h — abstract Binary Search Tree ADT (spec §4 row 7).
//
// StockBST implements insert/find/remove and all three traversals RECURSIVELY
// (spec §1.6 — recursion is the graded approach here). AVLStockTree (§4 row 8)
// inherits from the same concrete StockBST and OVERRIDES insert/remove to add
// rotations — that override is the "polymorphism" leg of the OOP checklist: same
// interface, genuinely different balancing behaviour, side-by-side comparable in
// the Week-8 demo (plain BST degenerates to a line, AVL stays ~log n).
//
#include <cstddef>
#include <functional>

namespace papertrade {

template <typename K, typename V>
class BST {
public:
    using Visitor = std::function<void(const K&, const V&)>;

    virtual ~BST() = default;

    virtual void insert(const K& key, const V& value) = 0;
    virtual const V* find(const K& key) const = 0;  // nullptr if absent
    virtual bool remove(const K& key) = 0;          // false if absent

    virtual std::size_t size() const = 0;
    virtual int height() const = 0;  // -1 for empty; drives the AVL vs BST demo

    // Recursive traversals — same data, three orderings (Week-7 demo).
    virtual void inorder(const Visitor& visit) const = 0;
    virtual void preorder(const Visitor& visit) const = 0;
    virtual void postorder(const Visitor& visit) const = 0;
};

}  // namespace papertrade
