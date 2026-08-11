#pragma once
//
// AVLStockTree.h — self-balancing BST (spec §4 row 8).
//
// Inherits ALL of StockBST's recursive insert/remove/find/traversal machinery
// and changes exactly one thing: it overrides the protected `rebalance` hook the
// base calls on the way back up every insertion/deletion. Where the base merely
// refreshes cached heights, the AVL version also performs the four standard
// rotations (LL, RR, LR, RL) to keep every node's balance factor in [-1, 1].
//
// This is the textbook "same interface, different behaviour" demonstration:
//   StockBST<int,...>  fed 1,2,3,...,n  → height n-1 (a linked list)
//   AVLStockTree<int,...> fed the same  → height ~log2(n)
// Both are used through the identical BST<K,V> pointer in the Week-8 comparison.
//
// Complexity: insert/remove stay O(log n) because rotations are O(1) and at most
// O(log n) of them run per operation.
//
#include <cstdlib>
#include <memory>
#include <utility>

#include "papertrade/structures/StockBST.h"

namespace papertrade {

template <typename K, typename V>
class AVLStockTree : public StockBST<K, V> {
    using Base = StockBST<K, V>;
    using BNode = typename Base::BNode;

public:
    // Verifies the AVL invariant across the whole tree — handy for tests and for
    // asserting balance live in the demo. O(n).
    bool balanced() const { return checkBalanced(this->root_.get()); }

protected:
    std::unique_ptr<BNode> rebalance(std::unique_ptr<BNode> node) override {
        Base::updateHeight(node.get());
        const int bf = Base::balanceFactor(node.get());

        if (bf > 1) {  // left-heavy
            if (Base::balanceFactor(node->left.get()) < 0) {
                node->left = rotateLeft(std::move(node->left));  // LR
            }
            return rotateRight(std::move(node));  // LL
        }
        if (bf < -1) {  // right-heavy
            if (Base::balanceFactor(node->right.get()) > 0) {
                node->right = rotateRight(std::move(node->right));  // RL
            }
            return rotateLeft(std::move(node));  // RR
        }
        return node;  // already balanced
    }

private:
    std::unique_ptr<BNode> rotateRight(std::unique_ptr<BNode> y) {
        std::unique_ptr<BNode> x = std::move(y->left);
        y->left = std::move(x->right);
        Base::updateHeight(y.get());
        x->right = std::move(y);
        Base::updateHeight(x.get());
        return x;
    }

    std::unique_ptr<BNode> rotateLeft(std::unique_ptr<BNode> x) {
        std::unique_ptr<BNode> y = std::move(x->right);
        x->right = std::move(y->left);
        Base::updateHeight(x.get());
        y->left = std::move(x);
        Base::updateHeight(y.get());
        return y;
    }

    static bool checkBalanced(const BNode* n) {
        if (!n) return true;
        if (std::abs(Base::balanceFactor(n)) > 1) return false;
        return checkBalanced(n->left.get()) && checkBalanced(n->right.get());
    }
};

}  // namespace papertrade
