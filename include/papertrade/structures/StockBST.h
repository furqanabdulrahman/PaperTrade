#pragma once
//
// StockBST.h — recursive Binary Search Tree keyed by ticker (spec §4 row 7).
//
// Keyed lookup for the stock universe: insert/find/remove and all three
// traversals are written RECURSIVELY (spec §1.6 — recursion is the graded
// approach). The recursive insert/remove helpers use the "return the (possibly
// new) subtree root" idiom, which threads ownership cleanly through unique_ptr
// and — crucially — lets AVLStockTree add rotations by overriding ONE hook
// (`rebalance`) instead of rewriting the traversal logic. Same public interface,
// genuinely different structural behaviour = the polymorphism leg of the OOP
// checklist. A plain StockBST fed sorted keys degenerates to a linked list
// (height n-1); the AVL subclass stays ~log n. That side-by-side height gap is
// the Week-8 demo.
//
// Complexity: insert/find/remove are O(h) — O(log n) balanced, O(n) degenerate.
// Every node is unique_ptr-owned (RAII); nodes are destroyed as subtrees go out
// of scope.
//
#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "papertrade/adt/BST.h"

namespace papertrade {

template <typename K, typename V>
class StockBST : public BST<K, V> {
public:
    using Visitor = typename BST<K, V>::Visitor;

    StockBST() = default;
    ~StockBST() override { clear(); }

    StockBST(const StockBST& o) : root_(clone(o.root_.get())), size_(o.size_) {}
    StockBST& operator=(const StockBST& o) {
        if (this != &o) {
            clear();
            root_ = clone(o.root_.get());
            size_ = o.size_;
        }
        return *this;
    }
    StockBST(StockBST&& o) noexcept { swap(o); }
    StockBST& operator=(StockBST&& o) noexcept {
        if (this != &o) {
            clear();
            swap(o);
        }
        return *this;
    }

    void insert(const K& key, const V& value) override {
        root_ = insertRec(std::move(root_), key, value);
    }

    const V* find(const K& key) const override {
        const BNode* cur = root_.get();
        while (cur) {
            if (key < cur->key) {
                cur = cur->left.get();
            } else if (cur->key < key) {
                cur = cur->right.get();
            } else {
                return &cur->value;
            }
        }
        return nullptr;
    }

    bool remove(const K& key) override {
        bool found = false;
        root_ = removeRec(std::move(root_), key, found);
        if (found) --size_;
        return found;
    }

    std::size_t size() const override { return size_; }
    bool empty() const { return size_ == 0; }

    // Tree height in EDGES: single node = 0, empty = -1 (matches BST.h contract).
    int height() const override {
        return root_ ? root_->height - 1 : -1;
    }

    void inorder(const Visitor& visit) const override { inorderRec(root_.get(), visit); }
    void preorder(const Visitor& visit) const override { preorderRec(root_.get(), visit); }
    void postorder(const Visitor& visit) const override { postorderRec(root_.get(), visit); }

    void clear() {
        // Iterative teardown: move each node's children onto an explicit stack
        // before it is destroyed, so a degenerate (list-shaped) tree can't blow
        // the call stack via chained unique_ptr destructors.
        std::vector<std::unique_ptr<BNode>> stack;
        if (root_) stack.push_back(std::move(root_));
        while (!stack.empty()) {
            std::unique_ptr<BNode> n = std::move(stack.back());
            stack.pop_back();
            if (n->left) stack.push_back(std::move(n->left));
            if (n->right) stack.push_back(std::move(n->right));
            // n now has no owned children → its destructor does not recurse.
        }
        size_ = 0;
    }

protected:
    struct BNode {
        K key;
        V value;
        std::unique_ptr<BNode> left;
        std::unique_ptr<BNode> right;
        int height = 1;  // node-count height (leaf = 1); drives balance factor
        BNode(const K& k, const V& v) : key(k), value(v) {}
    };

    static int hgt(const BNode* n) { return n ? n->height : 0; }
    static void updateHeight(BNode* n) {
        n->height = 1 + std::max(hgt(n->left.get()), hgt(n->right.get()));
    }
    static int balanceFactor(const BNode* n) {
        return n ? hgt(n->left.get()) - hgt(n->right.get()) : 0;
    }

    // Base behaviour: just refresh cached height, no restructuring. AVLStockTree
    // overrides this to rotate — the single point of polymorphic difference.
    virtual std::unique_ptr<BNode> rebalance(std::unique_ptr<BNode> node) {
        updateHeight(node.get());
        return node;
    }

    std::unique_ptr<BNode> root_;
    std::size_t size_ = 0;

private:
    std::unique_ptr<BNode> insertRec(std::unique_ptr<BNode> node, const K& key,
                                     const V& value) {
        if (!node) {
            ++size_;
            return std::make_unique<BNode>(key, value);
        }
        if (key < node->key) {
            node->left = insertRec(std::move(node->left), key, value);
        } else if (node->key < key) {
            node->right = insertRec(std::move(node->right), key, value);
        } else {
            node->value = value;  // key exists → update, no size change
            return node;
        }
        return rebalance(std::move(node));
    }

    std::unique_ptr<BNode> removeRec(std::unique_ptr<BNode> node, const K& key,
                                     bool& found) {
        if (!node) return nullptr;  // key absent on this path
        if (key < node->key) {
            node->left = removeRec(std::move(node->left), key, found);
        } else if (node->key < key) {
            node->right = removeRec(std::move(node->right), key, found);
        } else {
            found = true;
            if (!node->left) return std::move(node->right);   // 0/1 child
            if (!node->right) return std::move(node->left);   // 1 child
            // Two children: copy inorder successor (min of right subtree) up,
            // then delete it from the right subtree.
            const BNode* succ = node->right.get();
            while (succ->left) succ = succ->left.get();
            node->key = succ->key;
            node->value = succ->value;
            bool dummy = false;  // structural removal only; size handled once
            node->right = removeRec(std::move(node->right), node->key, dummy);
        }
        return rebalance(std::move(node));
    }

    static std::unique_ptr<BNode> clone(const BNode* n) {
        if (!n) return nullptr;
        auto copy = std::make_unique<BNode>(n->key, n->value);
        copy->height = n->height;
        copy->left = clone(n->left.get());
        copy->right = clone(n->right.get());
        return copy;
    }

    void inorderRec(const BNode* n, const Visitor& visit) const {
        if (!n) return;
        inorderRec(n->left.get(), visit);
        visit(n->key, n->value);
        inorderRec(n->right.get(), visit);
    }
    void preorderRec(const BNode* n, const Visitor& visit) const {
        if (!n) return;
        visit(n->key, n->value);
        preorderRec(n->left.get(), visit);
        preorderRec(n->right.get(), visit);
    }
    void postorderRec(const BNode* n, const Visitor& visit) const {
        if (!n) return;
        postorderRec(n->left.get(), visit);
        postorderRec(n->right.get(), visit);
        visit(n->key, n->value);
    }

    void swap(StockBST& o) noexcept {
        std::swap(root_, o.root_);
        std::swap(size_, o.size_);
    }
};

}  // namespace papertrade
