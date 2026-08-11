#pragma once
//
// LiveMarketData.h — real quotes + candles with no API key required.
//
// Pulls from Yahoo Finance's public chart endpoint over the native HTTPS client,
// so it works on the existing toolchain with zero setup. All symbols are fetched
// once up front and cached; any failure falls back to the synthetic feed for
// that symbol, so the UI is never left blank or blocked.
//
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
    std::vector<double> candles(const std::string& symbol, int n) const override;
    const char* sourceName() const override;

private:
    void ensureLoaded() const;

    std::vector<std::string> symbols_;
    SyntheticMarketData fallback_;

    mutable bool loaded_ = false;
    mutable bool anyLive_ = false;
    mutable std::vector<Quote> quotes_;
    mutable std::map<std::string, std::vector<double>> candleCache_;
};

}  // namespace papertrade
