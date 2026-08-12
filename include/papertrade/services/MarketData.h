#pragma once
//
// MarketData.h — abstraction over the quote/candle source (spec §4 Phase 4).
//
// The UI and domain code depend only on this interface, never on Finnhub
// directly. SyntheticMarketData backs development and demos; FinnhubClient
// (compiled when PAPERTRADE_ENABLE_SSL is on) fetches live data over HTTPS.
// Swapping providers is a one-line change at the composition root.
//
#include <string>
#include <vector>

namespace papertrade {

struct Quote {
    std::string symbol;
    double last = 0.0;
    double pctChange = 0.0;  // day % change
};

// One trading day: a candlestick. `time` is a unix timestamp (seconds).
struct Bar {
    double time = 0.0;
    double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
};

class MarketDataService {
public:
    virtual ~MarketDataService() = default;

    // Current snapshot of the tracked universe.
    virtual std::vector<Quote> universe() const = 0;

    // Single-symbol live quote (for rate-controlled incremental refresh of a
    // large universe). Returns false if unavailable. Default: derive from
    // universe() so simple providers need not override.
    virtual bool quote(const std::string& symbol, Quote& out) const {
        for (const auto& q : universe())
            if (q.symbol == symbol) { out = q; return true; }
        return false;
    }

    // Recent closing prices for one symbol, oldest first (up to `n`).
    virtual std::vector<double> candles(const std::string& symbol, int n) const = 0;

    // OHLC candlesticks for a time range ("5d", "1mo", "3mo", "1y"), oldest
    // first. Default builds flat bars from candles(); providers with real OHLC
    // override this.
    virtual std::vector<Bar> bars(const std::string& symbol,
                                  const std::string& range) const {
        const int n = range == "5d" ? 5 : range == "1mo" ? 22 : range == "3mo" ? 66 : 252;
        std::vector<double> c = candles(symbol, n);
        std::vector<Bar> out;
        for (std::size_t i = 0; i < c.size(); ++i)
            out.push_back({1.6e9 + static_cast<double>(i) * 86400.0, c[i], c[i], c[i], c[i]});
        return out;
    }

    // Human label for the status bar ("synthetic" vs "Finnhub live").
    virtual const char* sourceName() const = 0;
};

}  // namespace papertrade
