//
// LiveMarketData.cpp — see LiveMarketData.h. Parses Yahoo Finance chart JSON.
//
#include "papertrade/services/LiveMarketData.h"

#include <nlohmann/json.hpp>

#include "papertrade/services/HttpClient.h"

namespace papertrade {

using json = nlohmann::json;

LiveMarketData::LiveMarketData() {
    for (const auto& q : fallback_.universe()) symbols_.push_back(q.symbol);
}

const char* LiveMarketData::sourceName() const {
    ensureLoaded();
    return anyLive_ ? "Yahoo Finance (live)" : "synthetic (offline)";
}

void LiveMarketData::ensureLoaded() const {
    if (loaded_) return;
    loaded_ = true;

    for (const auto& sym : symbols_) {
        const std::string path =
            "/v8/finance/chart/" + sym + "?interval=1d&range=3mo";
        const HttpResponse res = httpsGet("query1.finance.yahoo.com", path);

        bool ok = false;
        if (res.ok()) {
            const json j = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
            if (!j.is_discarded() && j.contains("chart") &&
                j["chart"].contains("result") && j["chart"]["result"].is_array() &&
                !j["chart"]["result"].empty()) {
                const json& r = j["chart"]["result"][0];
                const json& meta = r.value("meta", json::object());
                const double last = meta.value("regularMarketPrice", 0.0);
                if (last > 0.0) {
                    std::vector<double> series;
                    if (r.contains("indicators") &&
                        r["indicators"].contains("quote") &&
                        r["indicators"]["quote"].is_array() &&
                        !r["indicators"]["quote"].empty() &&
                        r["indicators"]["quote"][0].contains("close")) {
                        for (const auto& c : r["indicators"]["quote"][0]["close"]) {
                            if (c.is_number()) series.push_back(c.get<double>());
                        }
                    }

                    // Daily % change: last price vs the prior day's close.
                    double prevClose = meta.value("chartPreviousClose", last);
                    if (series.size() >= 2) prevClose = series[series.size() - 2];
                    const double pct =
                        prevClose != 0.0 ? (last - prevClose) / prevClose * 100.0 : 0.0;

                    quotes_.push_back({sym, last, pct});
                    if (!series.empty()) candleCache_[sym] = series;
                    ok = true;
                    anyLive_ = true;
                }
            }
        }
        if (!ok) {
            // Fall back to synthetic for this one symbol.
            for (const auto& q : fallback_.universe())
                if (q.symbol == sym) {
                    quotes_.push_back(q);
                    break;
                }
        }
    }
}

std::vector<Quote> LiveMarketData::universe() const {
    ensureLoaded();
    return quotes_;
}

std::vector<double> LiveMarketData::candles(const std::string& symbol, int n) const {
    ensureLoaded();
    const auto it = candleCache_.find(symbol);
    if (it == candleCache_.end() || it->second.empty())
        return fallback_.candles(symbol, n);

    const std::vector<double>& all = it->second;
    if (n <= 0 || static_cast<std::size_t>(n) >= all.size()) return all;
    return std::vector<double>(all.end() - n, all.end());  // most recent n
}

}  // namespace papertrade
