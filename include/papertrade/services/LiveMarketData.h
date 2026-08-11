#pragma once
//
// LiveMarketData.h — real quotes + candles with no API key required.
//
// Pulls from Yahoo Finance's public chart endpoint over the native HTTPS client,
// so it works on the existing toolchain with zero setup. universe() is a pure
// fetch (no shared mutable state) so a background thread can poll it for live
// price updates; candles are lazily cached on the UI thread. Any failure falls
// back to the synthetic feed so the UI is never blank.
//
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "papertrade/services/MarketData.h"
#include "papertrade/services/SyntheticMarketData.h"

namespace papertrade {

class LiveMarketData : public MarketDataService {
public:
    LiveMarketData();

    std::vector<Quote> universe() const override;
    bool quote(const std::string& symbol, Quote& out) const override;
    std::vector<double> candles(const std::string& symbol, int n) const override;
    const char* sourceName() const override;

private:
    // Pure network fetch for one symbol. No shared state touched.
    bool fetchOne(const std::string& symbol, Quote& quote,
                  std::vector<double>& series) const;

    std::vector<std::string> symbols_;
    SyntheticMarketData fallback_;
    mutable std::atomic<bool> anyLive_{false};
    mutable std::map<std::string, std::vector<double>> candleCache_;  // UI thread only
};

}  // namespace papertrade
