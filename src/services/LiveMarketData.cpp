//
// LiveMarketData.cpp — see LiveMarketData.h. Parses Yahoo Finance chart JSON.
//
#include "papertrade/services/LiveMarketData.h"

#include <nlohmann/json.hpp>

#include "papertrade/domain/Universe.h"
#include "papertrade/services/HttpClient.h"

namespace papertrade {

using json = nlohmann::json;

LiveMarketData::LiveMarketData() {
    for (const auto& c : universeList()) symbols_.push_back(c.symbol);
}

bool LiveMarketData::quote(const std::string& symbol, Quote& out) const {
    std::vector<double> series;
    if (fetchOne(symbol, out, series)) {
        anyLive_.store(true);
        return true;
    }
    return fallback_.quote(symbol, out);  // synthetic placeholder
}

const char* LiveMarketData::sourceName() const {
    return anyLive_.load() ? "Yahoo Finance (live)" : "synthetic (offline)";
}

bool LiveMarketData::fetchOne(const std::string& symbol, Quote& quote,
                              std::vector<double>& series) const {
    const std::string path = "/v8/finance/chart/" + symbol + "?interval=1d&range=3mo";
    const HttpResponse res = httpsGet("query1.finance.yahoo.com", path);
    if (!res.ok()) return false;

    const json j = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.contains("chart") || !j["chart"].contains("result") ||
        !j["chart"]["result"].is_array() || j["chart"]["result"].empty())
        return false;

    const json& r = j["chart"]["result"][0];
    const json& meta = r.value("meta", json::object());
    const double last = meta.value("regularMarketPrice", 0.0);
    if (last <= 0.0) return false;

    series.clear();
    if (r.contains("indicators") && r["indicators"].contains("quote") &&
        r["indicators"]["quote"].is_array() && !r["indicators"]["quote"].empty() &&
        r["indicators"]["quote"][0].contains("close")) {
        for (const auto& c : r["indicators"]["quote"][0]["close"])
            if (c.is_number()) series.push_back(c.get<double>());
    }

    double prevClose = meta.value("chartPreviousClose", last);
    if (series.size() >= 2) prevClose = series[series.size() - 2];
    const double pct = prevClose != 0.0 ? (last - prevClose) / prevClose * 100.0 : 0.0;

    quote = {symbol, last, pct};
    return true;
}

std::vector<Quote> LiveMarketData::universe() const {
    std::vector<Quote> out;
    bool live = false;
    for (const auto& sym : symbols_) {
        Quote q;
        std::vector<double> series;
        if (fetchOne(sym, q, series)) {
            out.push_back(q);
            live = true;
        } else {
            for (const auto& s : fallback_.universe())
                if (s.symbol == sym) {
                    out.push_back(s);
                    break;
                }
        }
    }
    anyLive_.store(live);
    return out;
}

std::vector<Bar> LiveMarketData::bars(const std::string& symbol,
                                      const std::string& range) const {
    const std::string path =
        "/v8/finance/chart/" + symbol + "?interval=1d&range=" + range;
    const HttpResponse res = httpsGet("query1.finance.yahoo.com", path);
    std::vector<Bar> out;
    if (res.ok()) {
        const json j = json::parse(res.body, nullptr, false);
        if (!j.is_discarded() && j.contains("chart") && j["chart"].contains("result") &&
            j["chart"]["result"].is_array() && !j["chart"]["result"].empty()) {
            const json& r = j["chart"]["result"][0];
            if (r.contains("timestamp") && r.contains("indicators") &&
                r["indicators"].contains("quote") && r["indicators"]["quote"].is_array() &&
                !r["indicators"]["quote"].empty()) {
                const json& ts = r["timestamp"];
                const json& q = r["indicators"]["quote"][0];
                const std::size_t n = ts.size();
                for (std::size_t i = 0; i < n; ++i) {
                    if (!q["open"][i].is_number() || !q["close"][i].is_number() ||
                        !q["high"][i].is_number() || !q["low"][i].is_number())
                        continue;
                    Bar b;
                    b.time = ts[i].get<double>();
                    b.open = q["open"][i].get<double>();
                    b.high = q["high"][i].get<double>();
                    b.low = q["low"][i].get<double>();
                    b.close = q["close"][i].get<double>();
                    out.push_back(b);
                }
            }
        }
    }
    if (out.empty()) return fallback_.bars(symbol, range);  // synthetic
    return out;
}

std::vector<double> LiveMarketData::candles(const std::string& symbol, int n) const {
    auto it = candleCache_.find(symbol);
    if (it == candleCache_.end()) {
        Quote q;
        std::vector<double> series;
        if (fetchOne(symbol, q, series) && !series.empty())
            it = candleCache_.emplace(symbol, series).first;
    }
    if (it == candleCache_.end() || it->second.empty())
        return fallback_.candles(symbol, n);

    const std::vector<double>& all = it->second;
    if (n <= 0 || static_cast<std::size_t>(n) >= all.size()) return all;
    return std::vector<double>(all.end() - n, all.end());
}

}  // namespace papertrade
