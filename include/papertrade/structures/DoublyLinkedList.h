#pragma once
//
// DoublyLinkedList.h — concrete List<T> with backward links (spec §4 row 2).
//
// Base for RecentlyViewed (watchlist back/forward navigation). `next` pointers
// own the chain (unique_ptr, RAII); `prev` pointers are raw/non-owning to avoid
// an ownership cycle — the standard modern-C++ modelling of a doubly linked
// list, and a good talking point (owning both directions would double-free).
//
// Members are `protected` so derived views (RecentlyViewed) can run a cursor
// over the nodes. Complexity: push/pop at both ends O(1); traversal O(n).
//
#include <cstddef>
#include <memory>
#include <utility>

#include "papertrade/adt/List.h"
#include "papertrade/adt/Node.h"

namespace papertrade {

template <typename T>
class DoublyLinkedList : public List<T> {
protected:
    struct DNode : Node<T> {
        std::unique_ptr<DNode> next;  // owns successor
        DNode* prev = nullptr;        // non-owning back-edge
        explicit DNode(const T& v) : Node<T>(v) {}
        explicit DNode(T&& v) : Node<T>(std::move(v)) {}
    };

public:
    DoublyLinkedList() = default;
    ~DoublyLinkedList() override { clear(); }

    DoublyLinkedList(const DoublyLinkedList&) = delete;
    DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;
    DoublyLinkedList(DoublyLinkedList&& o) noexcept { swap(o); }
    DoublyLinkedList& operator=(DoublyLinkedList&& o) noexcept {
        if (this != &o) {
            clear();
            swap(o);
        }
        return *this;
    }

    void pushFront(const T& value) override {
        auto node = std::make_unique<DNode>(value);
        DNode* raw = node.get();
        if (!head_) {
            tail_ = raw;
        } else {
            head_->prev = raw;
            node->next = std::move(head_);
        }
        head_ = std::move(node);
        ++size_;
    }

    void pushBack(const T& value) override {
        auto node = std::make_unique<DNode>(value);
        DNode* raw = node.get();
        if (!head_) {
            head_ = std::move(node);
        } else {
            raw->prev = tail_;
            tail_->next = std::move(node);
        }
        tail_ = raw;
        ++size_;
    }

    bool popFront() override {
        if (!head_) return false;
        head_ = std::move(head_->next);
        if (head_) {
            head_->prev = nullptr;
        } else {
            tail_ = nullptr;
        }
        --size_;
        return true;
    }

    bool popBack() {
        if (!tail_) return false;
        if (tail_->prev == nullptr) {  // single element
            head_.reset();
            tail_ = nullptr;
        } else {
            DNode* newTail = tail_->prev;
            newTail->next.reset();  // frees old tail
            tail_ = newTail;
        }
        --size_;
        return true;
    }

    T& front() override { return head_->value; }
    const T& front() const override { return head_->value; }
    T& back() { return tail_->value; }
    const T& back() const { return tail_->value; }

    std::size_t size() const override { return size_; }
    bool empty() const override { return size_ == 0; }

    void clear() override {
        while (head_) head_ = std::move(head_->next);  // iterative
        tail_ = nullptr;
        size_ = 0;
    }

    template <typename F>
    void forEach(F&& fn) const {
        for (const DNode* cur = head_.get(); cur; cur = cur->next.get()) {
            fn(cur->value);
        }
    }

    template <typename F>
    void forEachReverse(F&& fn) const {
        for (const DNode* cur = tail_; cur; cur = cur->prev) fn(cur->value);
    }

protected:
    DNode* headNode() const { return head_.get(); }
    DNode* tailNode() const { return tail_; }

    void swap(DoublyLinkedList& o) noexcept {
        std::swap(head_, o.head_);
        std::swap(tail_, o.tail_);
        std::swap(size_, o.size_);
    }

    std::unique_ptr<DNode> head_;
    DNode* tail_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace papertrade
