#pragma once
//
// FinnhubClient.h — live market data over Finnhub's REST API (spec §4 Phase 4).
//
// Real HTTPS fetching is compiled only when PAPERTRADE_ENABLE_SSL is defined
// (cpp-httplib + OpenSSL, which needs the 64-bit toolchain). Until then — and
// whenever a request fails or the key is missing — it transparently falls back
// to SyntheticMarketData, so the rest of the app is never blocked on network or
// toolchain state.
//
#include <string>
#include <vector>

#include "papertrade/services/LiveMarketData.h"
#include "papertrade/services/MarketData.h"

namespace papertrade {

class FinnhubClient : public MarketDataService {
public:
    explicit FinnhubClient(std::string apiKey,
                           std::vector<std::string> symbols = {});

    std::vector<Quote> universe() const override;
    std::vector<double> candles(const std::string& symbol, int n) const override;
    const char* sourceName() const override;

    // True only when built with SSL support and a non-empty key.
    bool live() const;

private:
    std::string apiKey_;
    std::vector<std::string> symbols_;
    LiveMarketData fallback_;  // Yahoo: real candles + quote fallback
};

}  // namespace papertrade
