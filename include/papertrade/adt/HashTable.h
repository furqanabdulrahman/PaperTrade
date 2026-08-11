#pragma once
//
// HashTable.h — abstract hash-map ADT (spec §4 row 11).
//
// The concrete SeparateChainingHashTable<K,V> (Phase 8) supplies its own hash
// function and resolves collisions by chaining, resizing on load factor. It is
// instantiated twice: HashTable<string,Quote> (shared ticker→quote cache) and
// HashTable<string,UserSession> (per-user session lookup — the multi-tenancy
// mechanism of §2). O(1) average get/put.
//
#include <cstddef>

namespace papertrade {

template <typename K, typename V>
class HashTable {
public:
    virtual ~HashTable() = default;

    virtual void put(const K& key, const V& value) = 0;
    virtual V* get(const K& key) = 0;              // nullptr if absent
    virtual const V* get(const K& key) const = 0;
    virtual bool contains(const K& key) const = 0;
    virtual bool remove(const K& key) = 0;         // false if absent

    virtual std::size_t size() const = 0;
    virtual bool empty() const = 0;
};

}  // namespace papertrade
