//
// test_trees.cpp — invariants for Phase-5 tree types (spec §4 rows 6-8).
//
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "papertrade/structures/AVLStockTree.h"
#include "papertrade/structures/SectorTree.h"
#include "papertrade/structures/StockBST.h"

using namespace papertrade;

namespace {
// Collect keys via inorder; for a BST this must come out sorted.
template <typename Tree>
std::vector<int> inorderKeys(const Tree& t) {
    std::vector<int> keys;
    t.inorder([&](const int& k, const auto&) { keys.push_back(k); });
    return keys;
}
}  // namespace

// --- StockBST ---------------------------------------------------------------
TEST_CASE("StockBST insert/find and inorder is sorted", "[bst]") {
    StockBST<int, std::string> t;
    REQUIRE(t.empty());
    REQUIRE(t.height() == -1);

    for (int k : {5, 3, 8, 1, 4, 7, 9, 2, 6}) t.insert(k, "v" + std::to_string(k));
    REQUIRE(t.size() == 9);

    REQUIRE(t.find(4) != nullptr);
    REQUIRE(*t.find(4) == "v4");
    REQUIRE(t.find(42) == nullptr);

    REQUIRE(inorderKeys(t) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("StockBST insert on existing key updates value, not size", "[bst]") {
    StockBST<int, std::string> t;
    t.insert(1, "a");
    t.insert(1, "b");
    REQUIRE(t.size() == 1);
    REQUIRE(*t.find(1) == "b");
}

TEST_CASE("StockBST traversal orderings differ correctly", "[bst]") {
    StockBST<int, int> t;
    for (int k : {2, 1, 3}) t.insert(k, k);
    std::vector<int> pre, post;
    t.preorder([&](const int& k, const int&) { pre.push_back(k); });
    t.postorder([&](const int& k, const int&) { post.push_back(k); });
    REQUIRE(pre == std::vector<int>{2, 1, 3});
    REQUIRE(post == std::vector<int>{1, 3, 2});
}

TEST_CASE("StockBST remove handles leaf, one-child, two-child", "[bst]") {
    StockBST<int, int> t;
    for (int k : {5, 3, 8, 1, 4, 7, 9}) t.insert(k, k);

    REQUIRE(t.remove(1));  // leaf
    REQUIRE(t.find(1) == nullptr);
    REQUIRE(t.size() == 6);

    REQUIRE(t.remove(8));  // two children (7,9) → successor 9 promoted
    REQUIRE(t.find(8) == nullptr);
    REQUIRE(t.find(7) != nullptr);
    REQUIRE(t.find(9) != nullptr);

    REQUIRE(t.remove(3));  // one child (4)
    REQUIRE(t.find(3) == nullptr);
    REQUIRE(t.find(4) != nullptr);

    REQUIRE(inorderKeys(t) == std::vector<int>{4, 5, 7, 9});
    REQUIRE_FALSE(t.remove(1234));  // absent
}

TEST_CASE("StockBST degenerates to a chain on sorted input", "[bst][demo]") {
    StockBST<int, int> t;
    for (int k = 1; k <= 7; ++k) t.insert(k, k);
    // No balancing → each node is the right child of the previous: height n-1.
    REQUIRE(t.height() == 6);
}

TEST_CASE("StockBST deep copy is independent", "[bst]") {
    StockBST<int, int> a;
    for (int k : {2, 1, 3}) a.insert(k, k * 10);
    StockBST<int, int> b = a;  // copy
    b.insert(4, 40);
    REQUIRE(a.size() == 3);
    REQUIRE(b.size() == 4);
    REQUIRE(a.find(4) == nullptr);
    REQUIRE(*b.find(1) == 10);
}

// --- AVLStockTree -----------------------------------------------------------
TEST_CASE("AVL stays balanced and ~log n on sorted input", "[avl][demo]") {
    AVLStockTree<int, int> t;
    for (int k = 1; k <= 7; ++k) t.insert(k, k);
    // Same input that made StockBST height 6 → AVL is a balanced tree of 7.
    REQUIRE(t.size() == 7);
    REQUIRE(t.height() == 2);  // perfectly balanced 7-node tree
    REQUIRE(t.balanced());
    REQUIRE(inorderKeys(t) == std::vector<int>{1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("AVL stays balanced across a larger ascending run", "[avl]") {
    AVLStockTree<int, int> t;
    for (int k = 1; k <= 1023; ++k) t.insert(k, k);
    REQUIRE(t.size() == 1023);
    REQUIRE(t.balanced());
    // A balanced tree of 1023 nodes has height 9; AVL's bound keeps it ≤ ~13.
    REQUIRE(t.height() <= 10);
}

TEST_CASE("AVL triggers each rotation kind and keeps invariant", "[avl]") {
    {  // LL
        AVLStockTree<int, int> t;
        for (int k : {3, 2, 1}) t.insert(k, k);
        REQUIRE(t.balanced());
        REQUIRE(t.height() == 1);
    }
    {  // RR
        AVLStockTree<int, int> t;
        for (int k : {1, 2, 3}) t.insert(k, k);
        REQUIRE(t.balanced());
        REQUIRE(t.height() == 1);
    }
    {  // LR
        AVLStockTree<int, int> t;
        for (int k : {3, 1, 2}) t.insert(k, k);
        REQUIRE(t.balanced());
        REQUIRE(t.height() == 1);
    }
    {  // RL
        AVLStockTree<int, int> t;
        for (int k : {1, 3, 2}) t.insert(k, k);
        REQUIRE(t.balanced());
        REQUIRE(t.height() == 1);
    }
}

TEST_CASE("AVL remove keeps the tree balanced and sorted", "[avl]") {
    AVLStockTree<int, int> t;
    for (int k = 1; k <= 31; ++k) t.insert(k, k);
    for (int k = 1; k <= 15; ++k) REQUIRE(t.remove(k));
    REQUIRE(t.size() == 16);
    REQUIRE(t.balanced());
    REQUIRE(inorderKeys(t).front() == 16);
    REQUIRE(inorderKeys(t).back() == 31);
}

TEST_CASE("AVL is usable through a BST<K,V> base pointer", "[avl][polymorphism]") {
    AVLStockTree<int, int> avl;
    BST<int, int>& asBst = avl;  // same interface
    for (int k = 1; k <= 7; ++k) asBst.insert(k, k);
    REQUIRE(asBst.height() == 2);  // balanced behaviour via the base handle
    REQUIRE(asBst.find(4) != nullptr);
}

// --- SectorTree -------------------------------------------------------------
TEST_CASE("SectorTree builds a Sector/Sub-sector/Company hierarchy", "[sector]") {
    SectorTree tree;
    REQUIRE(tree.empty());

    tree.addCompany("Technology", "Semiconductors", "NVDA");
    tree.addCompany("Technology", "Semiconductors", "AMD");
    tree.addCompany("Technology", "Software", "MSFT");
    tree.addCompany("Financials", "Banks", "JPM");

    // Nodes created: Technology, Semiconductors, NVDA, AMD, Software, MSFT,
    // Financials, Banks, JPM = 9 (root excluded).
    REQUIRE(tree.size() == 9);
    REQUIRE_FALSE(tree.empty());
}

TEST_CASE("SectorTree shares intermediate nodes and is idempotent", "[sector]") {
    SectorTree tree;
    tree.addCompany("Technology", "Semiconductors", "NVDA");
    tree.addCompany("Technology", "Semiconductors", "NVDA");  // duplicate
    REQUIRE(tree.size() == 3);  // no duplicate branch created

    tree.addCompany("Technology", "Semiconductors", "AMD");
    REQUIRE(tree.size() == 4);  // only the new leaf is added
}

TEST_CASE("SectorTree tickersInSector collects all leaves under a sector",
          "[sector]") {
    SectorTree tree;
    tree.addCompany("Technology", "Semiconductors", "NVDA");
    tree.addCompany("Technology", "Semiconductors", "AMD");
    tree.addCompany("Technology", "Software", "MSFT");
    tree.addCompany("Financials", "Banks", "JPM");

    auto tech = tree.tickersInSector("Technology");
    REQUIRE(tech.size() == 3);
    // order follows insertion / DFS: NVDA, AMD, MSFT
    REQUIRE(tech == std::vector<std::string>{"NVDA", "AMD", "MSFT"});

    REQUIRE(tree.tickersInSector("Healthcare").empty());  // unknown sector
}

TEST_CASE("SectorTree clear empties the hierarchy", "[sector]") {
    SectorTree tree;
    tree.addCompany("Technology", "Semiconductors", "NVDA");
    tree.clear();
    REQUIRE(tree.empty());
    REQUIRE(tree.size() == 0);
    REQUIRE(tree.tickersInSector("Technology").empty());
}
