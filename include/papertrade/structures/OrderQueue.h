#pragma once
//
// OrderQueue.h — concrete FIFO Queue<T> (spec §4 row 4).
//
// One SHARED instance: the OrderEngine dequeues every user's orders in
// submission order (each order carries its own userId, §5). A queue is the
// natural model of a single processing pipeline serving many tenants.
//
// Backing choice (documented): LINKED via SinglyLinkedList<T>. enqueue is
// pushBack and dequeue is popFront — both true O(1) with no amortization and no
// wasted capacity, which is what you want for a pipeline that continuously grows
// at one end and shrinks at the other.
//
#include <cstddef>
#include <stdexcept>

#include "papertrade/adt/Queue.h"
#include "papertrade/structures/SinglyLinkedList.h"

namespace papertrade {

template <typename T>
class OrderQueue : public Queue<T> {
public:
    void enqueue(const T& value) override { list_.pushBack(value); }

    bool dequeue() override { return list_.popFront(); }

    T& front() override {
        if (list_.empty()) throw std::out_of_range("front() on empty OrderQueue");
        return list_.front();
    }
    const T& front() const override {
        if (list_.empty()) throw std::out_of_range("front() on empty OrderQueue");
        return list_.front();
    }

    bool empty() const override { return list_.empty(); }
    std::size_t size() const override { return list_.size(); }

private:
    SinglyLinkedList<T> list_;
};

}  // namespace papertrade
