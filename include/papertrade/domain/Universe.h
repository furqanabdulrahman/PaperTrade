#pragma once
//
// Universe.h — the tradable company list with sector classification. Single
// source of truth shared by the data providers and the UI so symbols, names and
// sectors never drift apart.
//
// Size is capped at ~30 sector-diverse large-caps so the background refresh can
// cycle the whole list every ~35s while staying comfortably under the live API's
// ~60 requests/minute free-tier limit.
//
#include <string>
#include <vector>

namespace papertrade {

struct Company {
    const char* symbol;
    const char* name;
    const char* sector;
    const char* sub;
};

inline const std::vector<Company>& universeList() {
    static const std::vector<Company> k = {
        {"AAPL", "Apple", "Technology", "Hardware"},
        {"MSFT", "Microsoft", "Technology", "Software"},
        {"NVDA", "NVIDIA", "Technology", "Semiconductors"},
        {"GOOG", "Alphabet", "Communication", "Internet"},
        {"META", "Meta Platforms", "Communication", "Internet"},
        {"NFLX", "Netflix", "Communication", "Entertainment"},
        {"AMZN", "Amazon", "Consumer", "Retail"},
        {"TSLA", "Tesla", "Consumer", "Autos"},
        {"MCD", "McDonald's", "Consumer", "Restaurants"},
        {"KO", "Coca-Cola", "Staples", "Beverages"},
        {"PG", "Procter & Gamble", "Staples", "Household"},
        {"WMT", "Walmart", "Staples", "Retail"},
        {"JPM", "JPMorgan Chase", "Financials", "Banks"},
        {"BAC", "Bank of America", "Financials", "Banks"},
        {"GS", "Goldman Sachs", "Financials", "Investment Banks"},
        {"JNJ", "Johnson & Johnson", "Healthcare", "Pharma"},
        {"UNH", "UnitedHealth", "Healthcare", "Insurance"},
        {"PFE", "Pfizer", "Healthcare", "Pharma"},
        {"BA", "Boeing", "Industrials", "Aerospace"},
        {"CAT", "Caterpillar", "Industrials", "Machinery"},
        {"HON", "Honeywell", "Industrials", "Conglomerate"},
        {"XOM", "Exxon Mobil", "Energy", "Oil & Gas"},
        {"CVX", "Chevron", "Energy", "Oil & Gas"},
        {"COP", "ConocoPhillips", "Energy", "Oil & Gas"},
        {"NEE", "NextEra Energy", "Utilities", "Electric"},
        {"DUK", "Duke Energy", "Utilities", "Electric"},
        {"SO", "Southern Company", "Utilities", "Electric"},
        {"LIN", "Linde", "Materials", "Chemicals"},
        {"SHW", "Sherwin-Williams", "Materials", "Chemicals"},
        {"FCX", "Freeport-McMoRan", "Materials", "Mining"},
    };
    return k;
}

}  // namespace papertrade
