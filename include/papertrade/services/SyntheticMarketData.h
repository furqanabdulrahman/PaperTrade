#pragma once
//
// SyntheticMarketData.h — deterministic, dependency-free market feed over the
// full company universe. Backs development, offline use, and the instant startup
// snapshot before live prices stream in. Each symbol gets a stable seeded
// random-walk so quotes and candles are reproducible.
//
#include <cstdint>
#include <string>
#include <vector>

#include "papertrade/domain/Universe.h"
#include "papertrade/services/MarketData.h"

namespace papertrade {

class SyntheticMarketData : public MarketDataService {
public:
    std::vector<Quote> universe() const override {
        std::vector<Quote> out;
        out.reserve(universeList().size());
        for (const auto& c : universeList()) {
            Quote q;
            quote(c.symbol, q);
            out.push_back(q);
        }
        return out;
    }

    bool quote(const std::string& symbol, Quote& out) const override {
        const std::vector<double> c = candles(symbol, 2);
        const double base = baseFor(symbol);
        const double last = c.empty() ? base : c.back();
        const double prev = c.size() >= 2 ? c[c.size() - 2] : base;
        const double pct = prev != 0.0 ? (last - prev) / prev * 100.0 : 0.0;
        out = {symbol, last, pct};
        return true;
    }

    std::vector<double> candles(const std::string& symbol, int n) const override {
        std::vector<double> out;
        if (n <= 0) return out;
        out.reserve(static_cast<std::size_t>(n));
        double price = baseFor(symbol);
        std::uint64_t s = seedFor(symbol);
        for (int i = 0; i < n; ++i) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;  // LCG
            const double r = ((s >> 33) & 0xFFFF) / 65535.0 - 0.5;    // [-0.5, 0.5)
            price *= (1.0 + r * 0.04);
            if (price < 1.0) price = 1.0;
            out.push_back(price);
        }
        return out;
    }

    const char* sourceName() const override { return "synthetic"; }

private:
    static std::uint64_t seedFor(const std::string& symbol) {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : symbol) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h ? h : 1;
    }

    // A stable, symbol-derived starting price in roughly $15–$600.
    static double baseFor(const std::string& symbol) {
        return 15.0 + static_cast<double>(seedFor(symbol) % 58500) / 100.0;
    }
};

}  // namespace papertrade
