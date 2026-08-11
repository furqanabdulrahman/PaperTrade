#pragma once
//
// SectorTree.h — general (n-ary) tree over the market hierarchy (spec §4 row 6).
//
// Models Market → Sector → Sub-sector → Company(ticker) so the UI can browse the
// 100-ticker universe by industry and roll quotes up to sector level. Unlike the
// BST, a general tree has an arbitrary fan-out, so each node keeps a vector of
// owning child pointers. A synthetic "Market" root always exists; size() counts
// the real nodes added beneath it.
//
// Complexity: addCompany walks/creates at most 3 levels, each a linear scan over
// siblings — O(children) per level, negligible for our fan-out. Traversal and
// tickersInSector are O(n) over the visited subtree. Teardown is iterative so a
// pathological chain can't overflow the stack.
//
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "papertrade/adt/Tree.h"

namespace papertrade {

class SectorTree : public Tree<std::string> {
public:
    struct SectorNode {
        std::string name;
        std::vector<std::unique_ptr<SectorNode>> children;
        explicit SectorNode(std::string n) : name(std::move(n)) {}
        bool leaf() const { return children.empty(); }
    };

    // Visits (name, depth) with the Market root at depth 0.
    using Visitor = std::function<void(const std::string&, int)>;

    SectorTree() : root_(std::make_unique<SectorNode>("Market")) {}

    bool empty() const override { return size_ == 0; }
    std::size_t size() const override { return size_; }  // excludes the root

    void clear() override {
        std::vector<std::unique_ptr<SectorNode>> stack;
        for (auto& c : root_->children) stack.push_back(std::move(c));
        root_->children.clear();
        while (!stack.empty()) {
            std::unique_ptr<SectorNode> n = std::move(stack.back());
            stack.pop_back();
            for (auto& c : n->children) stack.push_back(std::move(c));
            n->children.clear();  // destroyed with no owned children
        }
        size_ = 0;
    }

    // Insert a ticker under Sector → Sub-sector, creating intermediates as
    // needed. Re-adding the same ticker is a no-op (idempotent).
    void addCompany(const std::string& sector, const std::string& subSector,
                    const std::string& ticker) {
        SectorNode* s = childOrCreate(root_.get(), sector);
        SectorNode* ss = childOrCreate(s, subSector);
        childOrCreate(ss, ticker);
    }

    const SectorNode* root() const { return root_.get(); }

    // Collect every leaf ticker beneath the named sector (empty if not found).
    std::vector<std::string> tickersInSector(const std::string& sector) const {
        std::vector<std::string> out;
        const SectorNode* s = findChild(root_.get(), sector);
        if (s) collectLeaves(s, out);
        return out;
    }

    void forEach(const Visitor& visit) const { forEachRec(root_.get(), 0, visit); }

private:
    static SectorNode* findChild(const SectorNode* parent, const std::string& name) {
        for (const auto& c : parent->children) {
            if (c->name == name) return c.get();
        }
        return nullptr;
    }

    SectorNode* childOrCreate(SectorNode* parent, const std::string& name) {
        if (SectorNode* existing = findChild(parent, name)) return existing;
        parent->children.push_back(std::make_unique<SectorNode>(name));
        ++size_;
        return parent->children.back().get();
    }

    static void collectLeaves(const SectorNode* n, std::vector<std::string>& out) {
        if (n->leaf()) {
            out.push_back(n->name);
            return;
        }
        for (const auto& c : n->children) collectLeaves(c.get(), out);
    }

    static void forEachRec(const SectorNode* n, int depth, const Visitor& visit) {
        visit(n->name, depth);
        for (const auto& c : n->children) forEachRec(c.get(), depth + 1, visit);
    }

    std::unique_ptr<SectorNode> root_;
    std::size_t size_ = 0;
};

}  // namespace papertrade
