#pragma once
//
// PriceRefreshCycle.h — market-wide round-robin polling scheduler (spec §4 row 2,
// "CircularLinkedList → PriceRefreshCycle"; §6).
//
// One shared instance. The background refresh thread calls nextTicker() on each
// tick to decide which symbol to fetch from Finnhub, so the ~100-ticker universe
// is polled evenly and endlessly while staying under the 60-calls/min budget
// (the pacing/rate-limit lives in the refresh thread, Phase 4 — this class only
// owns the cyclic ordering).
//
#include <cstddef>
#include <string>

#include "papertrade/structures/CircularLinkedList.h"

namespace papertrade {

class PriceRefreshCycle {
public:
    void addTicker(const std::string& ticker) { cycle_.append(ticker); }

    // The next symbol to refresh, advancing the round-robin cursor.
    const std::string& nextTicker() { return cycle_.advance(); }

    const std::string& currentTicker() const { return cycle_.current(); }

    std::size_t size() const { return cycle_.size(); }
    bool empty() const { return cycle_.empty(); }

    template <typename F>
    void forEach(F&& fn) const {
        cycle_.forEach(std::forward<F>(fn));
    }

private:
    CircularLinkedList<std::string> cycle_;
};

}  // namespace papertrade
