#pragma once
//
// SyntheticMarketData.h — deterministic, dependency-free market feed.
//
// Backs development and the demo until the Finnhub client is enabled. Each
// symbol gets a stable seeded random-walk so quotes and candles are reproducible
// across runs (important for screenshots and tests). Spec §4 explicitly allows
// this synthetic fallback.
//
#include <cstdint>
#include <string>
#include <vector>

#include "papertrade/services/MarketData.h"

namespace papertrade {

class SyntheticMarketData : public MarketDataService {
public:
    SyntheticMarketData() {
        // A small, sector-diverse universe with plausible starting prices.
        seedRows_ = {
            {"NVDA", 121.40}, {"AMD", 168.90}, {"INTC", 21.75}, {"MSFT", 428.70},
            {"GOOG", 172.55}, {"META", 512.30}, {"AAPL", 229.35}, {"AMZN", 186.20},
            {"TSLA", 244.10}, {"JPM", 214.05},  {"KO", 62.10},    {"PFE", 28.40},
        };
    }

    std::vector<Quote> universe() const override {
        std::vector<Quote> out;
        out.reserve(seedRows_.size());
        for (const auto& row : seedRows_) {
            const std::vector<double> c = candles(row.symbol, 2);
            const double prev = c.size() >= 2 ? c[c.size() - 2] : row.base;
            const double last = c.empty() ? row.base : c.back();
            const double pct = prev != 0.0 ? (last - prev) / prev * 100.0 : 0.0;
            out.push_back({row.symbol, last, pct});
        }
        return out;
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
    struct Row {
        std::string symbol;
        double base;
    };

    double baseFor(const std::string& symbol) const {
        for (const auto& r : seedRows_)
            if (r.symbol == symbol) return r.base;
        return 100.0;
    }

    static std::uint64_t seedFor(const std::string& symbol) {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : symbol) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h ? h : 1;
    }

    std::vector<Row> seedRows_;
};

}  // namespace papertrade
