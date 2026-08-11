#pragma once
//
// List.h — abstract List ADT (spec §4 row 2).
//
// Pure-virtual interface implemented by SinglyLinkedList, DoublyLinkedList and
// CircularLinkedList in Phase 3. Defining the interface here, before any
// concrete class, is the "abstraction" leg of the OOP checklist: services can be
// written against List<T> without knowing which linkage a concrete list uses.
//
#include <cstddef>

namespace papertrade {

template <typename T>
class List {
public:
    virtual ~List() = default;

    virtual void pushFront(const T& value) = 0;
    virtual void pushBack(const T& value) = 0;
    virtual bool popFront() = 0;  // false if empty

    virtual T& front() = 0;
    virtual const T& front() const = 0;

    virtual std::size_t size() const = 0;
    virtual bool empty() const = 0;
    virtual void clear() = 0;
};

}  // namespace papertrade
