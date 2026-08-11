#pragma once
//
// CircularLinkedList.h — concrete round-robin list (spec §4 row 2).
//
// Backs PriceRefreshCycle, the market-wide polling scheduler. Ownership is a
// LINEAR unique_ptr chain (head_ … tail_); circularity is realised by wrapping
// the traversal cursor from the tail back to the head. Doing it this way keeps
// RAII intact — a literal owning tail→head edge would form a unique_ptr
// ownership cycle that never frees. The behaviour is a true ring: advance()
// cycles forever through the elements.
//
// Complexity: append O(1), advance O(1), traversal O(n).
//
#include <cstddef>
#include <memory>
#include <utility>

#include "papertrade/adt/Node.h"

namespace papertrade {

template <typename T>
class CircularLinkedList {
    struct CNode : Node<T> {
        std::unique_ptr<CNode> next;
        explicit CNode(const T& v) : Node<T>(v) {}
        explicit CNode(T&& v) : Node<T>(std::move(v)) {}
    };

public:
    CircularLinkedList() = default;
    ~CircularLinkedList() { clear(); }

    CircularLinkedList(const CircularLinkedList&) = delete;
    CircularLinkedList& operator=(const CircularLinkedList&) = delete;

    void append(const T& value) {
        auto node = std::make_unique<CNode>(value);
        CNode* raw = node.get();
        if (!head_) {
            head_ = std::move(node);
            cursor_ = raw;
        } else {
            tail_->next = std::move(node);
        }
        tail_ = raw;
        ++size_;
    }

    // Return the element under the cursor, then advance the cursor one step,
    // wrapping from tail back to head. This is the round-robin "next to fetch".
    const T& advance() {
        const T& value = cursor_->value;
        cursor_ = cursor_->next ? cursor_->next.get() : head_.get();
        return value;
    }

    const T& current() const { return cursor_->value; }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        while (head_) head_ = std::move(head_->next);  // iterative
        tail_ = nullptr;
        cursor_ = nullptr;
        size_ = 0;
    }

    template <typename F>
    void forEach(F&& fn) const {
        for (const CNode* cur = head_.get(); cur; cur = cur->next.get()) {
            fn(cur->value);
        }
    }

private:
    std::unique_ptr<CNode> head_;
    CNode* tail_ = nullptr;
    CNode* cursor_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace papertrade
