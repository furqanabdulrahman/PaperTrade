#pragma once
//
// Stack.h — abstract LIFO Stack ADT (spec §4 row 3).
// Implemented by OrderStack (order cancellation / undo-last-action) in Phase 3.
//
#include <cstddef>

namespace papertrade {

template <typename T>
class Stack {
public:
    virtual ~Stack() = default;

    virtual void push(const T& value) = 0;
    virtual bool pop() = 0;  // false if empty

    virtual T& top() = 0;
    virtual const T& top() const = 0;

    virtual bool empty() const = 0;
    virtual std::size_t size() const = 0;
};

}  // namespace papertrade
