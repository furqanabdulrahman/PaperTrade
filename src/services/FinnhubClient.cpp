//
// FinnhubClient.cpp — see FinnhubClient.h. Uses the native HTTPS client
// (WinHTTP), so a Finnhub key works on the existing toolchain — no OpenSSL.
//
#include "papertrade/services/FinnhubClient.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "papertrade/domain/Universe.h"
#include "papertrade/services/HttpClient.h"

namespace papertrade {

using json = nlohmann::json;

FinnhubClient::FinnhubClient(std::string apiKey, std::vector<std::string> symbols)
    : apiKey_(std::move(apiKey)), symbols_(std::move(symbols)) {
    if (symbols_.empty()) {
        for (const auto& c : universeList()) symbols_.push_back(c.symbol);
    }
}

bool FinnhubClient::live() const { return !apiKey_.empty(); }

bool FinnhubClient::quote(const std::string& symbol, Quote& out) const {
    if (!apiKey_.empty()) {
        const std::string path = "/api/v1/quote?symbol=" + symbol + "&token=" + apiKey_;
        const HttpResponse res = httpsGet("finnhub.io", path);
        if (res.ok()) {
            const json j = json::parse(res.body, nullptr, false);
            if (!j.is_discarded()) {
                const double last = j.value("c", 0.0);
                const double prev = j.value("pc", 0.0);
                if (last > 0.0) {
                    const double pct = prev != 0.0 ? (last - prev) / prev * 100.0 : 0.0;
                    out = {symbol, last, pct};
                    return true;
                }
            }
        }
    }
    return fallback_.quote(symbol, out);  // Yahoo, then synthetic
}

const char* FinnhubClient::sourceName() const {
    return live() ? "Finnhub (live)" : "synthetic (no key)";
}

std::vector<Quote> FinnhubClient::universe() const {
    if (!apiKey_.empty()) {
        std::vector<Quote> out;
        for (const auto& sym : symbols_) {
            const std::string path =
                "/api/v1/quote?symbol=" + sym + "&token=" + apiKey_;
            const HttpResponse res = httpsGet("finnhub.io", path);
            if (res.ok()) {
                const json j = json::parse(res.body, nullptr, false);
                if (!j.is_discarded()) {
                    const double last = j.value("c", 0.0);   // current
                    const double prev = j.value("pc", 0.0);  // previous close
                    if (last > 0.0) {
                        const double pct =
                            prev != 0.0 ? (last - prev) / prev * 100.0 : 0.0;
                        out.push_back({sym, last, pct});
                    }
                }
            }
        }
        if (!out.empty()) return out;
    }
    return fallback_.universe();
}

std::vector<double> FinnhubClient::candles(const std::string& symbol, int n) const {
    // Finnhub's candle endpoint is gated on paid plans, so charts use the
    // (real) Yahoo series from the fallback provider.
    return fallback_.candles(symbol, n);
}

std::vector<Bar> FinnhubClient::bars(const std::string& symbol, const std::string& range,
                                     const std::string& interval) const {
    return fallback_.bars(symbol, range, interval);  // real OHLC from Yahoo
}

}  // namespace papertrade
