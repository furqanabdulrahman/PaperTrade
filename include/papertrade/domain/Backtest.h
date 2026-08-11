#pragma once
//
// Backtest.h — dynamic-programming profit analysis over a price series
// (spec §4 row 14: "best time to buy/sell").
//
// Three classic formulations, all over our own DynamicArray<double>:
//   * bestSingle        — one buy + one sell, O(n) (min-so-far DP)
//   * unlimited         — any number of trades, O(n) (sum of up-runs)
//   * maxProfitAtMostK  — at most k trades, O(n·k) rolling-state DP
// bestSingle and unlimited also reconstruct the actual trades for the UI.
//
#include <cstddef>
#include <limits>
#include <vector>

#include "papertrade/adt/DynamicArray.h"

namespace papertrade {

struct Backtest {
    struct Trade {
        std::size_t buyDay = 0;
        std::size_t sellDay = 0;
        double buyPrice = 0.0;
        double sellPrice = 0.0;
        double profit() const { return sellPrice - buyPrice; }
    };

    struct Result {
        double profit = 0.0;
        std::vector<Trade> trades;
    };

    // Best single buy→sell. profit 0 and buyDay==sellDay if prices only fall.
    static Trade bestSingle(const DynamicArray<double>& p) {
        Trade best;
        if (p.size() < 2) return best;
        std::size_t minIdx = 0;
        for (std::size_t i = 1; i < p.size(); ++i) {
            if (p[i] - p[minIdx] > best.profit()) {
                best = {minIdx, i, p[minIdx], p[i]};
            }
            if (p[i] < p[minIdx]) minIdx = i;
        }
        return best;
    }

    // Unlimited transactions: capture every rising run as its own trade.
    static Result unlimited(const DynamicArray<double>& p) {
        Result r;
        const std::size_t n = p.size();
        std::size_t i = 0;
        while (i + 1 < n) {
            while (i + 1 < n && p[i + 1] <= p[i]) ++i;  // descend to a valley
            if (i + 1 >= n) break;
            const std::size_t buy = i;
            while (i + 1 < n && p[i + 1] >= p[i]) ++i;  // climb to a peak
            const std::size_t sell = i;
            r.trades.push_back({buy, sell, p[buy], p[sell]});
            r.profit += p[sell] - p[buy];
        }
        return r;
    }

    // Max profit with at most k transactions (DP). Reconstruction is omitted;
    // this returns the optimal total.
    static double maxProfitAtMostK(const DynamicArray<double>& p, int k) {
        const std::size_t n = p.size();
        if (n < 2 || k <= 0) return 0.0;

        // With k >= n/2 there is no transaction limit → greedy sum of up-runs.
        if (static_cast<std::size_t>(k) >= n / 2) {
            double profit = 0.0;
            for (std::size_t i = 1; i < n; ++i)
                if (p[i] > p[i - 1]) profit += p[i] - p[i - 1];
            return profit;
        }

        const double kNegInf = -std::numeric_limits<double>::infinity();
        std::vector<double> buy(k + 1, kNegInf);  // best balance holding a share
        std::vector<double> sell(k + 1, 0.0);     // best balance after selling
        for (std::size_t d = 0; d < n; ++d) {
            for (int j = 1; j <= k; ++j) {
                if (sell[j - 1] - p[d] > buy[j]) buy[j] = sell[j - 1] - p[d];
                if (buy[j] + p[d] > sell[j]) sell[j] = buy[j] + p[d];
            }
        }
        return sell[k];
    }
};

}  // namespace papertrade
