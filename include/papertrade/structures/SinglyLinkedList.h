#pragma once
//
// SinglyLinkedList.h — concrete List<T> (spec §4 row 2).
//
// Backs the per-user TransactionList (trade history log). Nodes are owned by a
// unique_ptr forward chain (RAII, §4 row 5): dropping the head frees the chain
// with no manual delete. Destruction/clear() unlink ITERATIVELY rather than
// letting unique_ptr recurse, so a long history can't overflow the stack.
//
// Complexity: pushFront / pushBack (tail-tracked) / popFront all O(1);
// traversal O(n).
//
#include <cstddef>
#include <memory>
#include <utility>

#include "papertrade/adt/List.h"
#include "papertrade/adt/Node.h"

namespace papertrade {

template <typename T>
class SinglyLinkedList : public List<T> {
    struct SNode : Node<T> {
        std::unique_ptr<SNode> next;
        explicit SNode(const T& v) : Node<T>(v) {}
        explicit SNode(T&& v) : Node<T>(std::move(v)) {}
    };

public:
    SinglyLinkedList() = default;
    ~SinglyLinkedList() override { clear(); }

    // Owning unique_ptr chain ⇒ not trivially copyable; movable via swap.
    SinglyLinkedList(const SinglyLinkedList&) = delete;
    SinglyLinkedList& operator=(const SinglyLinkedList&) = delete;
    SinglyLinkedList(SinglyLinkedList&& o) noexcept { swap(o); }
    SinglyLinkedList& operator=(SinglyLinkedList&& o) noexcept {
        if (this != &o) {
            clear();
            swap(o);
        }
        return *this;
    }

    void pushFront(const T& value) override {
        auto node = std::make_unique<SNode>(value);
        if (!head_) {
            tail_ = node.get();
        } else {
            node->next = std::move(head_);
        }
        head_ = std::move(node);
        ++size_;
    }

    void pushBack(const T& value) override {
        auto node = std::make_unique<SNode>(value);
        SNode* raw = node.get();
        if (!head_) {
            head_ = std::move(node);
        } else {
            tail_->next = std::move(node);
        }
        tail_ = raw;
        ++size_;
    }

    bool popFront() override {
        if (!head_) return false;
        head_ = std::move(head_->next);  // frees old head
        if (!head_) tail_ = nullptr;
        --size_;
        return true;
    }

    T& front() override { return head_->value; }
    const T& front() const override { return head_->value; }

    std::size_t size() const override { return size_; }
    bool empty() const override { return size_ == 0; }

    void clear() override {
        // Iterative unwinding: move each head out before it is destroyed, so the
        // unique_ptr destructor never recurses down the whole chain.
        while (head_) head_ = std::move(head_->next);
        tail_ = nullptr;
        size_ = 0;
    }

    // Visit values head → tail. Used by TransactionList export / traversal.
    template <typename F>
    void forEach(F&& fn) const {
        for (const SNode* cur = head_.get(); cur; cur = cur->next.get()) {
            fn(cur->value);
        }
    }

private:
    void swap(SinglyLinkedList& o) noexcept {
        std::swap(head_, o.head_);
        std::swap(tail_, o.tail_);
        std::swap(size_, o.size_);
    }

    std::unique_ptr<SNode> head_;
    SNode* tail_ = nullptr;  // non-owning, for O(1) pushBack
    std::size_t size_ = 0;
};

}  // namespace papertrade
