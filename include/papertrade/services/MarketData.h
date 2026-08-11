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

class MarketDataService {
public:
    virtual ~MarketDataService() = default;

    // Current snapshot of the tracked universe.
    virtual std::vector<Quote> universe() const = 0;

    // Recent closing prices for one symbol, oldest first (up to `n`).
    virtual std::vector<double> candles(const std::string& symbol, int n) const = 0;

    // Human label for the status bar ("synthetic" vs "Finnhub live").
    virtual const char* sourceName() const = 0;
};

}  // namespace papertrade
