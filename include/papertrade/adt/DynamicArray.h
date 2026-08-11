#pragma once
//
// DynamicArray.h — hand-built growable array (spec §4 row 1).
//
// This is a GRADED structure: it replaces std::vector everywhere in the domain
// layer and is the backing buffer for the array-based Heap (§4 row 9). It owns a
// raw heap buffer and follows the rule of five for correct copy/move semantics.
//
// Complexity: push_back amortized O(1) (doubling growth), operator[] O(1),
// insert/erase at index O(n).
//
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace papertrade {

template <typename T>
class DynamicArray {
public:
    DynamicArray() = default;

    explicit DynamicArray(std::size_t initialCapacity) { reserve(initialCapacity); }

    // --- Rule of five -------------------------------------------------------
    DynamicArray(const DynamicArray& other) { copyFrom(other); }

    DynamicArray& operator=(const DynamicArray& other) {
        if (this != &other) {
            clear();
            ::operator delete(buffer_);
            buffer_ = nullptr;
            capacity_ = 0;
            copyFrom(other);
        }
        return *this;
    }

    DynamicArray(DynamicArray&& other) noexcept
        : buffer_(other.buffer_), size_(other.size_), capacity_(other.capacity_) {
        other.buffer_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            destroyAll();
            ::operator delete(buffer_);
            buffer_ = other.buffer_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.buffer_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    ~DynamicArray() {
        destroyAll();
        ::operator delete(buffer_);
    }

    // --- Capacity -----------------------------------------------------------
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    void reserve(std::size_t newCapacity) {
        if (newCapacity <= capacity_) return;
        grow(newCapacity);
    }

    // --- Modifiers ----------------------------------------------------------
    void push_back(const T& value) {
        ensureCapacity();
        new (buffer_ + size_) T(value);  // placement-new: construct in place
        ++size_;
    }

    void push_back(T&& value) {
        ensureCapacity();
        new (buffer_ + size_) T(std::move(value));
        ++size_;
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        ensureCapacity();
        T* slot = new (buffer_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *slot;
    }

    void pop_back() {
        if (size_ == 0) throw std::out_of_range("pop_back on empty DynamicArray");
        --size_;
        buffer_[size_].~T();
    }

    void clear() { destroyAll(); }

    // --- Element access -----------------------------------------------------
    T& operator[](std::size_t i) { return buffer_[i]; }
    const T& operator[](std::size_t i) const { return buffer_[i]; }

    T& at(std::size_t i) {
        if (i >= size_) throw std::out_of_range("DynamicArray::at out of range");
        return buffer_[i];
    }
    const T& at(std::size_t i) const {
        if (i >= size_) throw std::out_of_range("DynamicArray::at out of range");
        return buffer_[i];
    }

    T& front() { return buffer_[0]; }
    const T& front() const { return buffer_[0]; }
    T& back() { return buffer_[size_ - 1]; }
    const T& back() const { return buffer_[size_ - 1]; }

    // Raw iterators so range-for and std-style algorithms (in test code) work.
    T* begin() { return buffer_; }
    T* end() { return buffer_ + size_; }
    const T* begin() const { return buffer_; }
    const T* end() const { return buffer_ + size_; }

private:
    T* buffer_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;

    void ensureCapacity() {
        if (size_ == capacity_) {
            grow(capacity_ == 0 ? 4 : capacity_ * 2);  // doubling ⇒ amortized O(1)
        }
    }

    void grow(std::size_t newCapacity) {
        // Allocate raw storage and move-construct existing elements over.
        T* newBuffer = static_cast<T*>(::operator new(newCapacity * sizeof(T)));
        for (std::size_t i = 0; i < size_; ++i) {
            new (newBuffer + i) T(std::move(buffer_[i]));
            buffer_[i].~T();
        }
        ::operator delete(buffer_);
        buffer_ = newBuffer;
        capacity_ = newCapacity;
    }

    void destroyAll() {
        for (std::size_t i = 0; i < size_; ++i) buffer_[i].~T();
        size_ = 0;
    }

    void copyFrom(const DynamicArray& other) {
        reserve(other.size_);
        for (std::size_t i = 0; i < other.size_; ++i) {
            new (buffer_ + i) T(other.buffer_[i]);
        }
        size_ = other.size_;
    }
};

}  // namespace papertrade
