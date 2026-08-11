#pragma once
//
// RecentlyViewed.h — per-user watchlist back/forward navigation (spec §4 row 2,
// "DoublyLinkedList → RecentlyViewed").
//
// Inherits DoublyLinkedList<string> and adds a browser-style cursor: visiting a
// new ticker truncates any forward history and appends, while back()/forward()
// walk the prev/next links. This is exactly why a DOUBLY linked list is the
// right structure — single links couldn't support O(1) backward navigation.
//
#include <string>

#include "papertrade/structures/DoublyLinkedList.h"

namespace papertrade {

class RecentlyViewed : public DoublyLinkedList<std::string> {
    using Base = DoublyLinkedList<std::string>;
    using NodeT = Base::DNode;

public:
    // Record a newly viewed ticker. Browser semantics: if we had navigated
    // "back" and then view something new, the forward history is discarded.
    void visit(const std::string& ticker) {
        if (cursor_ && cursor_->value == ticker) return;  // already here

        if (cursor_) {
            while (tailNode() != cursor_) popBack();  // drop forward history
        } else {
            clear();
        }
        pushBack(ticker);
        cursor_ = tailNode();
    }

    bool canBack() const { return cursor_ && cursor_->prev != nullptr; }
    bool canForward() const { return cursor_ && cursor_->next != nullptr; }

    const std::string* current() const {
        return cursor_ ? &cursor_->value : nullptr;
    }

    const std::string* back() {
        if (!canBack()) return nullptr;
        cursor_ = cursor_->prev;
        return &cursor_->value;
    }

    const std::string* forward() {
        if (!canForward()) return nullptr;
        cursor_ = cursor_->next.get();
        return &cursor_->value;
    }

private:
    NodeT* cursor_ = nullptr;  // non-owning; points into the base list
};

}  // namespace papertrade
