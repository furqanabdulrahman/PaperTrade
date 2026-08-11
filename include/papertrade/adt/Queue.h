#pragma once
//
// Queue.h — abstract FIFO Queue ADT (spec §4 row 4).
// Implemented by OrderQueue (the shared FIFO order-processing engine) in Phase 3.
//
#include <cstddef>

namespace papertrade {

template <typename T>
class Queue {
public:
    virtual ~Queue() = default;

    virtual void enqueue(const T& value) = 0;
    virtual bool dequeue() = 0;  // false if empty

    virtual T& front() = 0;
    virtual const T& front() const = 0;

    virtual bool empty() const = 0;
    virtual std::size_t size() const = 0;
};

}  // namespace papertrade
