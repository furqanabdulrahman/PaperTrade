#pragma once
//
// SeparateChainingHashTable.h — hash map with chaining (spec §4 row 11).
//
// Supplies its OWN hash (FNV-1a, not std::hash) and resolves collisions by
// chaining unique_ptr-linked entries per bucket, doubling the bucket array when
// the load factor passes 0.75 so get/put stay O(1) average. Bucket count is a
// power of two, so indexing is a mask, not a modulo.
//
// Instantiated across the app as ticker→Quote (shared cache) and
// userId→Portfolio (per-user multi-tenancy). Move-only: the bucket chains own
// their nodes, so the table is not trivially copyable by design.
//
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/adt/HashTable.h"

namespace papertrade {

// Our own hasher: FNV-1a over the bytes of the key (chars for std::string).
struct PtHash {
    std::size_t operator()(const std::string& s) const noexcept {
        std::size_t h = kOffset;
        for (unsigned char c : s) {
            h ^= c;
            h *= kPrime;
        }
        return h;
    }
    template <typename U>
    std::size_t operator()(const U& v) const noexcept {
        static_assert(std::is_arithmetic<U>::value,
                      "PtHash: provide a specialization for non-arithmetic keys");
        std::size_t h = kOffset;
        const auto* p = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            h ^= p[i];
            h *= kPrime;
        }
        return h;
    }

private:
    static constexpr std::size_t kOffset = 1469598103934665603ULL;
    static constexpr std::size_t kPrime = 1099511628211ULL;
};

template <typename K, typename V, typename Hasher = PtHash>
class SeparateChainingHashTable : public HashTable<K, V> {
public:
    explicit SeparateChainingHashTable(std::size_t initialBuckets = 8) {
        initBuckets(initialBuckets);
    }
    ~SeparateChainingHashTable() override { clear(); }

    // Deep copy — rehashes every entry into a fresh bucket array (the chains own
    // their nodes, so there is no shallow copy to fall back on). Value semantics
    // let move-free containers like Portfolio hold this table by value.
    SeparateChainingHashTable(const SeparateChainingHashTable& o) {
        initBuckets(o.bucketCount_);
        o.forEach([&](const K& k, const V& v) { put(k, v); });
    }
    SeparateChainingHashTable& operator=(const SeparateChainingHashTable& o) {
        if (this != &o) {
            clear();
            buckets_ = DynamicArray<std::unique_ptr<Node>>();
            size_ = 0;
            initBuckets(o.bucketCount_);
            o.forEach([&](const K& k, const V& v) { put(k, v); });
        }
        return *this;
    }
    SeparateChainingHashTable(SeparateChainingHashTable&&) noexcept = default;
    SeparateChainingHashTable& operator=(SeparateChainingHashTable&&) noexcept = default;

    void put(const K& key, const V& value) override {
        Node* n = findNode(key);
        if (n) {
            n->value = value;
            return;
        }
        const std::size_t i = indexOf(key);
        auto node = std::make_unique<Node>(key, value);
        node->next = std::move(buckets_[i]);
        buckets_[i] = std::move(node);
        ++size_;
        if (loadFactorExceeded()) resize(bucketCount_ * 2);
    }

    V* get(const K& key) override {
        Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }
    const V* get(const K& key) const override {
        const Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }

    bool contains(const K& key) const override { return findNode(key) != nullptr; }

    bool remove(const K& key) override {
        const std::size_t i = indexOf(key);
        std::unique_ptr<Node>* link = &buckets_[i];
        while (*link) {
            if ((*link)->key == key) {
                *link = std::move((*link)->next);  // unlink
                --size_;
                return true;
            }
            link = &(*link)->next;
        }
        return false;
    }

    // Returns the value for `key`, default-constructing (and inserting) it if
    // absent. Enables lazy per-user records (userId -> Portfolio). Requires V to
    // be default-constructible.
    V& getOrCreate(const K& key) {
        if (Node* n = findNode(key)) return n->value;
        const std::size_t i = indexOf(key);
        auto node = std::make_unique<Node>(key);
        node->next = std::move(buckets_[i]);
        buckets_[i] = std::move(node);
        ++size_;
        V& ref = buckets_[i]->value;
        if (loadFactorExceeded()) {
            resize(bucketCount_ * 2);
            return *get(key);  // node moved during rehash; re-fetch
        }
        return ref;
    }

    std::size_t size() const override { return size_; }
    bool empty() const override { return size_ == 0; }
    std::size_t bucketCount() const { return bucketCount_; }

    // Visit every (key, value) — order is unspecified (bucket order).
    template <typename F>
    void forEach(F&& fn) const {
        for (std::size_t b = 0; b < bucketCount_; ++b) {
            for (const Node* n = buckets_[b].get(); n; n = n->next.get()) {
                fn(n->key, n->value);
            }
        }
    }

    void clear() {
        for (std::size_t b = 0; b < bucketCount_; ++b) {
            // Iterative teardown so a long chain can't deep-recurse on destruct.
            std::unique_ptr<Node> n = std::move(buckets_[b]);
            while (n) n = std::move(n->next);
        }
        size_ = 0;
    }

private:
    struct Node {
        K key;
        V value;
        std::unique_ptr<Node> next;
        Node(const K& k, const V& v) : key(k), value(v) {}
        explicit Node(const K& k) : key(k), value() {}  // for getOrCreate
    };

    void initBuckets(std::size_t requested) {
        std::size_t n = 1;
        while (n < requested) n <<= 1;  // round up to a power of two
        for (std::size_t i = 0; i < n; ++i) buckets_.push_back(nullptr);
        bucketCount_ = n;
    }

    std::size_t indexOf(const K& key) const {
        return hasher_(key) & (bucketCount_ - 1);
    }

    Node* findNode(const K& key) const {
        for (Node* n = buckets_[indexOf(key)].get(); n; n = n->next.get()) {
            if (n->key == key) return n;
        }
        return nullptr;
    }

    bool loadFactorExceeded() const {
        return size_ * 4 > bucketCount_ * 3;  // > 0.75
    }

    void resize(std::size_t newCount) {
        DynamicArray<std::unique_ptr<Node>> nb;
        for (std::size_t i = 0; i < newCount; ++i) nb.push_back(nullptr);
        for (std::size_t b = 0; b < bucketCount_; ++b) {
            std::unique_ptr<Node> cur = std::move(buckets_[b]);
            while (cur) {
                std::unique_ptr<Node> next = std::move(cur->next);
                const std::size_t ni = hasher_(cur->key) & (newCount - 1);
                cur->next = std::move(nb[ni]);
                nb[ni] = std::move(cur);
                cur = std::move(next);
            }
        }
        buckets_ = std::move(nb);
        bucketCount_ = newCount;
    }

    DynamicArray<std::unique_ptr<Node>> buckets_;
    std::size_t bucketCount_ = 0;
    std::size_t size_ = 0;
    Hasher hasher_{};
};

}  // namespace papertrade
