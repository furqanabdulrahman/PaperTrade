//
// Persistence.cpp — see Persistence.h. JSON serialization of the portfolio.
//
#include "papertrade/services/Persistence.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>

namespace papertrade {

using json = nlohmann::json;

bool savePortfolio(const Portfolio& pf, const std::string& path) {
    const Portfolio::Snapshot s = pf.snapshot();
    json j;
    j["cash"] = s.cash;
    j["realizedPnl"] = s.realizedPnl;
    j["nextId"] = s.nextId;
    for (const auto& kv : s.holdings)
        j["holdings"].push_back(
            {{"sym", kv.first}, {"qty", kv.second.qty}, {"avgCost", kv.second.avgCost}});
    for (const auto& o : s.history)
        j["history"].push_back({{"id", o.id},
                                {"ticker", o.ticker},
                                {"side", static_cast<int>(o.side)},
                                {"qty", o.qty},
                                {"price", o.price}});
    for (const auto& po : s.pending)
        j["pending"].push_back({{"id", po.id},
                                {"ticker", po.ticker},
                                {"side", static_cast<int>(po.side)},
                                {"qty", po.qty},
                                {"limit", po.limitPrice}});

    try {
        const std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    } catch (const std::exception&) {
        // best-effort; fall through to the write attempt
    }

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return static_cast<bool>(f);
}

bool loadPortfolio(Portfolio& pf, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    json j;
    try {
        f >> j;
    } catch (const std::exception&) {
        return false;
    }

    Portfolio::Snapshot s;
    s.cash = j.value("cash", 100000.0);
    s.realizedPnl = j.value("realizedPnl", 0.0);
    s.nextId = j.value("nextId", 1L);

    if (j.contains("holdings"))
        for (const auto& h : j["holdings"]) {
            Position p;
            p.qty = h.value("qty", 0.0);
            p.avgCost = h.value("avgCost", 0.0);
            s.holdings.push_back({h.value("sym", std::string()), p});
        }
    if (j.contains("history"))
        for (const auto& o : j["history"]) {
            Order od;
            od.id = o.value("id", 0L);
            od.ticker = o.value("ticker", std::string());
            od.side = static_cast<Side>(o.value("side", 0));
            od.qty = o.value("qty", 0.0);
            od.price = o.value("price", 0.0);
            s.history.push_back(od);
        }
    if (j.contains("pending"))
        for (const auto& po : j["pending"]) {
            PendingOrder p;
            p.id = po.value("id", 0L);
            p.ticker = po.value("ticker", std::string());
            p.side = static_cast<Side>(po.value("side", 0));
            p.qty = po.value("qty", 0.0);
            p.limitPrice = po.value("limit", 0.0);
            s.pending.push_back(p);
        }

    pf.restore(s);
    return true;
}

}  // namespace papertrade
