#pragma once
//
// AccountBook.h — multi-tenant registry of per-user portfolios (spec §2).
//
// The multi-tenancy mechanism: a SeparateChainingHashTable maps userId -> its
// own Portfolio, created lazily on first access. Each user's cash and holdings
// are fully isolated. O(1) average lookup per request.
//
#include <cstddef>
#include <string>

#include "papertrade/domain/Portfolio.h"
#include "papertrade/structures/SeparateChainingHashTable.h"

namespace papertrade {

class AccountBook {
public:
    // The user's portfolio, created with the starting balance on first access.
    Portfolio& portfolio(const std::string& userId) {
        return book_.getOrCreate(userId);
    }

    bool exists(const std::string& userId) const { return book_.contains(userId); }
    std::size_t userCount() const { return book_.size(); }

private:
    SeparateChainingHashTable<std::string, Portfolio> book_;
};

}  // namespace papertrade
