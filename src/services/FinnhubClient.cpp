//
// FinnhubClient.cpp — see FinnhubClient.h. HTTPS paths compile only under
// PAPERTRADE_ENABLE_SSL; otherwise every method delegates to the synthetic feed.
//
#include "papertrade/services/FinnhubClient.h"

#include <utility>

#ifdef PAPERTRADE_ENABLE_SSL
#include <httplib.h>
#include <nlohmann/json.hpp>
#endif

namespace papertrade {

FinnhubClient::FinnhubClient(std::string apiKey, std::vector<std::string> symbols)
    : apiKey_(std::move(apiKey)), symbols_(std::move(symbols)) {
    if (symbols_.empty()) {
        for (const auto& q : fallback_.universe()) symbols_.push_back(q.symbol);
    }
}

bool FinnhubClient::live() const {
#ifdef PAPERTRADE_ENABLE_SSL
    return !apiKey_.empty();
#else
    return false;
#endif
}

const char* FinnhubClient::sourceName() const {
    return live() ? "Finnhub (live)" : "synthetic (Finnhub disabled)";
}

std::vector<Quote> FinnhubClient::universe() const {
#ifdef PAPERTRADE_ENABLE_SSL
    if (!apiKey_.empty()) {
        httplib::Client cli("https://finnhub.io");
        cli.set_connection_timeout(3);
        std::vector<Quote> out;
        for (const auto& sym : symbols_) {
            const std::string path =
                "/api/v1/quote?symbol=" + sym + "&token=" + apiKey_;
            if (auto res = cli.Get(path.c_str()); res && res->status == 200) {
                auto j = nlohmann::json::parse(res->body, nullptr, false);
                if (!j.is_discarded()) {
                    const double last = j.value("c", 0.0);   // current
                    const double prev = j.value("pc", 0.0);  // previous close
                    const double pct =
                        prev != 0.0 ? (last - prev) / prev * 100.0 : 0.0;
                    out.push_back({sym, last, pct});
                }
            }
        }
        if (!out.empty()) return out;  // partial success still beats synthetic
    }
#endif
    return fallback_.universe();
}

std::vector<double> FinnhubClient::candles(const std::string& symbol, int n) const {
    // The candle endpoint needs from/to unix timestamps + resolution; wiring
    // that up is deferred until live access is enabled. Synthetic meanwhile.
    return fallback_.candles(symbol, n);
}

}  // namespace papertrade
