#pragma once
//
// Persistence.h — save/load a Portfolio to a JSON file so account state
// (cash, positions, order history, resting limit orders, realized P&L) survives
// closing the app. Resting limit orders are re-evaluated against live prices on
// the next launch.
//
#include <string>
#include <vector>

#include "papertrade/domain/Portfolio.h"

namespace papertrade {

// Persist the portfolio-value time series (parallel timestamp/value vectors) so
// the tracker shows real history across sessions.
bool saveEquityHistory(const std::vector<double>& times, const std::vector<double>& values,
                       const std::string& path);
bool loadEquityHistory(std::vector<double>& times, std::vector<double>& values,
                       const std::string& path);

// Writes the portfolio to `path` (creating parent directories). Returns false
// on I/O failure.
bool savePortfolio(const Portfolio& pf, const std::string& path);

// Loads the portfolio from `path` into `pf`. Returns false if the file is
// missing or malformed (in which case `pf` is left unchanged).
bool loadPortfolio(Portfolio& pf, const std::string& path);

}  // namespace papertrade
