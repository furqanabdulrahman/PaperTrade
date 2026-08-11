//
// main.cpp — PaperTrade desktop application (Dear ImGui + GLFW/OpenGL, ImPlot).
//
// A native brokerage-style GUI over pt_core. Live prices refresh on a background
// thread, so resting limit orders fill automatically when the market crosses the
// target. Every screen is driven by the real graded structures:
//   Trade      — watchlist, market + limit order ticket, positions, open orders,
//                order history (Portfolio/AccountBook order engine)
//   Dashboard  — MaxHeap/MinHeap movers, SectorTree, ImPlot price chart
//   Sort Lab   — all 8 Sorter<T> strategies with live comparison/move counts
//   Backtest   — DP max-profit (bestSingle / unlimited / at-most-k)
//   Graph      — StockGraph BFS + minimum spanning tree
// Data source: Finnhub if FINNHUB_API_KEY is in .env, else keyless Yahoo, else
// synthetic — all over WinHTTP. `--smoke` renders headless; `--probe` prints
// live quotes.
//
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/domain/AccountBook.h"
#include "papertrade/domain/Backtest.h"
#include "papertrade/services/FinnhubClient.h"
#include "papertrade/services/LiveMarketData.h"
#include "papertrade/services/SyntheticMarketData.h"
#include "papertrade/structures/MaxHeap.h"
#include "papertrade/structures/MinHeap.h"
#include "papertrade/structures/SectorTree.h"
#include "papertrade/structures/StockGraph.h"
#include "papertrade/structures/sorters/ComparisonSorts.h"
#include "papertrade/structures/sorters/NonComparisonSorts.h"
#include "papertrade/util/Env.h"

using namespace papertrade;

namespace {

// ---- Palette ---------------------------------------------------------------
const ImVec4 kGreen(0.24f, 0.78f, 0.44f, 1.0f);
const ImVec4 kRed(0.94f, 0.38f, 0.38f, 1.0f);
const ImVec4 kMuted(0.60f, 0.63f, 0.69f, 1.0f);
ImVec4 pnlColor(double v) { return v >= 0 ? kGreen : kRed; }

// ---- Static sector taxonomy for the universe -------------------------------
struct SectorOf { const char* sym; const char* sector; const char* sub; };
const SectorOf kTaxonomy[] = {
    {"NVDA", "Technology", "Semiconductors"}, {"AMD", "Technology", "Semiconductors"},
    {"INTC", "Technology", "Semiconductors"}, {"MSFT", "Technology", "Software"},
    {"GOOG", "Technology", "Internet"},       {"META", "Technology", "Internet"},
    {"AAPL", "Technology", "Hardware"},        {"AMZN", "Consumer", "Retail"},
    {"TSLA", "Consumer", "Autos"},             {"KO", "Consumer", "Beverages"},
    {"JPM", "Financials", "Banks"},            {"PFE", "Healthcare", "Pharma"},
};

SectorTree buildSectorTree() {
    SectorTree t;
    for (const auto& e : kTaxonomy) t.addCompany(e.sector, e.sub, e.sym);
    return t;
}

StockGraph buildSectorGraph() {
    StockGraph g;
    for (const auto& e : kTaxonomy) g.addVertex(e.sym);
    const int n = static_cast<int>(sizeof(kTaxonomy) / sizeof(kTaxonomy[0]));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const bool sameSub = std::strcmp(kTaxonomy[i].sub, kTaxonomy[j].sub) == 0;
            const bool sameSec = std::strcmp(kTaxonomy[i].sector, kTaxonomy[j].sector) == 0;
            if (sameSub) g.addEdge(kTaxonomy[i].sym, kTaxonomy[j].sym, 0.2);
            else if (sameSec) g.addEdge(kTaxonomy[i].sym, kTaxonomy[j].sym, 0.6);
        }
    return g;
}

// ---- App state -------------------------------------------------------------
struct App {
    std::unique_ptr<MarketDataService> market;
    std::vector<Quote> quotes;
    AccountBook accounts;
    SectorTree sectors = buildSectorTree();
    StockGraph graph = buildSectorGraph();
    std::string user = "you";

    int selected = 0;
    int orderTypeIdx = 0;  // 0 = Market, 1 = Limit
    int sideIdx = 0;       // 0 = Buy, 1 = Sell
    int tradeQty = 10;
    double limitPrice = 0.0;
    std::string tradeMsg;
    std::vector<std::string> events;

    int sortSize = 400;
    std::vector<int> sortSeed;

    // Background price refresh
    std::mutex mtx;
    std::vector<Quote> incoming;
    std::atomic<bool> hasFresh{false};
    std::atomic<bool> running{true};
    std::thread refreshThread;
    double lastRefresh = 0.0;

    explicit App(std::unique_ptr<MarketDataService> provider)
        : market(std::move(provider)) {
        quotes = market->universe();
        regenSortData();
    }
    ~App() {
        running.store(false);
        if (refreshThread.joinable()) refreshThread.join();
    }

    void startRefresh() {
        refreshThread = std::thread([this] {
            while (running.load()) {
                for (int i = 0; i < 200 && running.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // ~20s
                if (!running.load()) break;
                auto q = market->universe();
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    incoming = std::move(q);
                }
                hasFresh.store(true);
            }
        });
    }

    // Called each frame on the UI thread: apply fresh prices and fill any
    // resting limit orders they trigger.
    void pumpPrices() {
        if (!hasFresh.exchange(false)) return;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!incoming.empty()) quotes = std::move(incoming);
        }
        lastRefresh = glfwGetTime();
        Portfolio& pf = accounts.portfolio(user);
        auto filled = pf.evaluate([this](const std::string& s) { return priceOf(s); });
        for (const auto& o : filled) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Limit filled: %s %.0f %s @ %.2f",
                          toString(o.side), o.qty, o.ticker.c_str(), o.price);
            events.insert(events.begin(), buf);
        }
    }

    void regenSortData() {
        sortSeed.clear();
        std::uint64_t s = 88172645463325252ULL + static_cast<std::uint64_t>(sortSize);
        for (int i = 0; i < sortSize; ++i) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            sortSeed.push_back(static_cast<int>(s % 100000));
        }
    }

    double priceOf(const std::string& sym) const {
        for (const auto& q : quotes)
            if (q.symbol == sym) return q.last;
        return 0.0;
    }
    const std::string& selectedSymbol() const { return quotes[selected].symbol; }
};

// ---- Small helpers ---------------------------------------------------------
void pctText(double pct) {
    ImGui::TextColored(pnlColor(pct), "%+.2f%%", pct);
}

std::vector<Quote> topMovers(const std::vector<Quote>& all, int k, bool gainers) {
    const auto byPct = [](const Quote& a, const Quote& b) { return a.pctChange < b.pctChange; };
    std::vector<Quote> out;
    if (gainers) {
        MaxHeap<Quote> h(byPct);
        for (const auto& q : all) h.push(q);
        for (int i = 0; i < k && !h.empty(); ++i) out.push_back(h.pop());
    } else {
        MinHeap<Quote> h(byPct);
        for (const auto& q : all) h.push(q);
        for (int i = 0; i < k && !h.empty(); ++i) out.push_back(h.pop());
    }
    return out;
}

void drawSectorNode(const SectorTree::SectorNode* n) {
    if (n->leaf()) { ImGui::BulletText("%s", n->name.c_str()); return; }
    if (ImGui::TreeNodeEx(n->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& c : n->children) drawSectorNode(c.get());
        ImGui::TreePop();
    }
}

void symbolCombo(App& a) {
    if (ImGui::BeginCombo("Symbol", a.selectedSymbol().c_str())) {
        for (int i = 0; i < static_cast<int>(a.quotes.size()); ++i) {
            const bool sel = i == a.selected;
            if (ImGui::Selectable(a.quotes[i].symbol.c_str(), sel)) a.selected = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// ---- Account summary bar ---------------------------------------------------
void accountBar(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const double equity = pf.marketValue([&](const std::string& s) { return a.priceOf(s); });
    double unreal = 0.0;
    for (const auto& q : a.quotes)
        if (const Position* p = pf.position(q.symbol))
            unreal += (q.last - p->avgCost) * p->qty;

    if (ImGui::BeginTable("acct", 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        auto cell = [](const char* label, const std::string& value, ImVec4 col) {
            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%s", label);
            ImGui::TextColored(col, "%s", value.c_str());
        };
        char b[48];
        std::snprintf(b, sizeof(b), "$%.2f", pf.cash());        cell("CASH", b, ImVec4(1,1,1,1));
        std::snprintf(b, sizeof(b), "$%.2f", equity);           cell("EQUITY", b, ImVec4(1,1,1,1));
        std::snprintf(b, sizeof(b), "%+.2f", unreal);           cell("UNREALIZED P&L", b, pnlColor(unreal));
        std::snprintf(b, sizeof(b), "%+.2f", pf.realizedPnl()); cell("REALIZED P&L", b, pnlColor(pf.realizedPnl()));
        cell("DATA", a.market->sourceName(), kGreen);
        const double age = a.lastRefresh > 0 ? glfwGetTime() - a.lastRefresh : -1;
        std::snprintf(b, sizeof(b), age < 0 ? "startup" : "%.0fs ago", age);
        cell("UPDATED", b, kMuted);
        ImGui::EndTable();
    }
}

// ---- Trade tab -------------------------------------------------------------
void watchlist(App& a) {
    if (ImGui::BeginTable("watch", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg");
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(a.quotes.size()); ++i) {
            const Quote& q = a.quotes[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(q.symbol.c_str(), a.selected == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                a.selected = i;
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", q.last);
            ImGui::TableSetColumnIndex(2); pctText(q.pctChange);
        }
        ImGui::EndTable();
    }
}

void orderTicket(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const std::string sym = a.selectedSymbol();
    const double last = a.priceOf(sym);

    ImGui::Text("%s", sym.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  last %.2f", last);

    ImGui::RadioButton("Market", &a.orderTypeIdx, 0); ImGui::SameLine();
    ImGui::RadioButton("Limit", &a.orderTypeIdx, 1);

    ImGui::RadioButton("Buy", &a.sideIdx, 0); ImGui::SameLine();
    ImGui::RadioButton("Sell", &a.sideIdx, 1);

    ImGui::SetNextItemWidth(160);
    ImGui::InputInt("Quantity", &a.tradeQty);
    if (a.tradeQty < 1) a.tradeQty = 1;

    if (a.orderTypeIdx == 1) {
        if (a.limitPrice <= 0.0) a.limitPrice = last;
        ImGui::SetNextItemWidth(160);
        ImGui::InputDouble("Limit price", &a.limitPrice, 0.0, 0.0, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("= last")) a.limitPrice = last;
        ImGui::TextColored(kMuted, a.sideIdx == 0
                                       ? "Buys automatically when price falls to your limit."
                                       : "Sells automatically when price rises to your limit.");
    } else {
        ImGui::TextColored(kMuted, "Est. cost: %.2f", last * a.tradeQty);
    }

    const Side side = a.sideIdx == 0 ? Side::Buy : Side::Sell;
    if (ImGui::Button("Place order", ImVec2(160, 0))) {
        Portfolio::Result r;
        if (a.orderTypeIdx == 0)
            r = side == Side::Buy ? pf.buy(sym, a.tradeQty, last)
                                  : pf.sell(sym, a.tradeQty, last);
        else
            r = pf.placeLimit(sym, side, a.tradeQty, a.limitPrice);
        a.tradeMsg = r.message;
        if (r.ok) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s %s %d %s%s", r.message.c_str(),
                          toString(side), a.tradeQty, sym.c_str(),
                          a.orderTypeIdx == 1 ? " (resting)" : "");
            a.events.insert(a.events.begin(), buf);
        }
    }
    if (!a.tradeMsg.empty()) ImGui::TextColored(kMuted, "%s", a.tradeMsg.c_str());
}

void positionsTable(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    if (ImGui::BeginTable("pos", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Avg cost");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("Mkt value");
        ImGui::TableSetupColumn("Unreal P&L");
        ImGui::TableHeadersRow();
        bool any = false;
        for (const auto& q : a.quotes) {
            const Position* p = pf.position(q.symbol);
            if (!p) continue;
            any = true;
            const double pnl = (q.last - p->avgCost) * p->qty;
            const double pct = p->avgCost != 0 ? (q.last - p->avgCost) / p->avgCost * 100 : 0;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(q.symbol.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.0f", p->qty);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", p->avgCost);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", q.last);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", p->qty * q.last);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextColored(pnlColor(pnl), "%+.2f (%+.1f%%)", pnl, pct);
        }
        ImGui::EndTable();
        if (!any) ImGui::TextColored(kMuted, "No open positions yet.");
    }
}

void openOrdersTable(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    if (pf.pendingCount() == 0) {
        ImGui::TextColored(kMuted, "No resting limit orders.");
        return;
    }
    if (ImGui::BeginTable("open", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Limit");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        long cancelId = -1;
        for (std::size_t i = 0; i < pf.pendingCount(); ++i) {
            const PendingOrder& o = pf.pendingAt(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(o.side == Side::Buy ? kGreen : kRed, "%s", toString(o.side));
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(o.ticker.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", o.qty);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", o.limitPrice);
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(static_cast<int>(o.id));
            if (ImGui::SmallButton("Cancel")) cancelId = o.id;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (cancelId >= 0) pf.cancelPending(cancelId);
    }
}

void historyTable(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const std::size_t n = pf.orderCount();
    if (n == 0) { ImGui::TextColored(kMuted, "No orders yet."); return; }
    if (ImGui::BeginTable("hist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Price");
        ImGui::TableHeadersRow();
        const std::size_t start = n > 12 ? n - 12 : 0;
        for (std::size_t i = n; i-- > start;) {
            const Order& o = pf.orderAt(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(o.side == Side::Buy ? kGreen : kRed, "%s", toString(o.side));
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(o.ticker.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", o.qty);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", o.price);
        }
        ImGui::EndTable();
    }
}

void tabTrade(App& a) {
    ImGui::Columns(2, "trade", true);
    ImGui::SeparatorText("Watchlist");
    watchlist(a);
    if (ImGui::Button("Undo last trade")) {
        if (a.accounts.portfolio(a.user).undoLast()) a.tradeMsg = "Undid last trade";
    }
    ImGui::NextColumn();
    ImGui::SeparatorText("Order ticket");
    orderTicket(a);
    ImGui::Columns(1);

    ImGui::SeparatorText("Positions");
    positionsTable(a);
    ImGui::Columns(2, "orders", true);
    ImGui::SeparatorText("Open orders");
    openOrdersTable(a);
    ImGui::NextColumn();
    ImGui::SeparatorText("Recent orders");
    historyTable(a);
    ImGui::Columns(1);
}

// ---- Other tabs ------------------------------------------------------------
void moverTable(const char* id, const std::vector<Quote>& rows) {
    if (ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol"); ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg"); ImGui::TableHeadersRow();
        for (const auto& q : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(q.symbol.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", q.last);
            ImGui::TableSetColumnIndex(2); pctText(q.pctChange);
        }
        ImGui::EndTable();
    }
}

void tabDashboard(App& a) {
    ImGui::Columns(2, "dash", true);
    ImGui::SeparatorText("Top Gainers");
    moverTable("g", topMovers(a.quotes, 5, true));
    ImGui::SeparatorText("Top Losers");
    moverTable("l", topMovers(a.quotes, 5, false));
    ImGui::Spacing();
    symbolCombo(a);
    const std::vector<double> series = a.market->candles(a.selectedSymbol(), 60);
    ImGui::SeparatorText((a.selectedSymbol() + " — price history").c_str());
    if (ImPlot::BeginPlot("##dashchart", ImVec2(-1, 240))) {
        ImPlot::SetupAxes("session", "price", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("close", series.data(), static_cast<int>(series.size()));
        ImPlot::EndPlot();
    }
    ImGui::NextColumn();
    ImGui::SeparatorText("Sectors");
    for (const auto& c : a.sectors.root()->children) drawSectorNode(c.get());
    ImGui::Columns(1);
}

void tabSortLab(App& a) {
    ImGui::SeparatorText("Sorting benchmark");
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Array size", &a.sortSize);
    if (a.sortSize < 2) a.sortSize = 2;
    if (a.sortSize > 5000) a.sortSize = 5000;
    ImGui::SameLine();
    if (ImGui::Button("Regenerate")) a.regenSortData();
    ImGui::TextColored(kMuted, "Same random array fed to every algorithm; counters reset per run.");

    struct Row { const char* name; std::size_t comps, moves; };
    std::vector<Row> rows;
    auto run = [&](Sorter<int>& s) {
        DynamicArray<int> d;
        for (int x : a.sortSeed) d.push_back(x);
        s.sort(d, [](const int& x, const int& y) { return x < y; });
        rows.push_back({s.name(), s.comparisons(), s.moves()});
    };
    BubbleSort<int> bub; SelectionSort<int> sel; InsertionSort<int> ins;
    MergeSort<int> mrg; QuickSort<int> qk; HeapSort<int> hp;
    CountingSort<int> cnt; RadixSort<int> rdx;
    run(bub); run(sel); run(ins); run(mrg); run(qk); run(hp); run(cnt); run(rdx);

    if (ImGui::BeginTable("sorts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Algorithm"); ImGui::TableSetupColumn("Comparisons");
        ImGui::TableSetupColumn("Moves"); ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.name);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", r.comps);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", r.moves);
        }
        ImGui::EndTable();
    }
    std::vector<double> comps; std::vector<const char*> labels;
    for (const auto& r : rows) { comps.push_back(static_cast<double>(r.comps)); labels.push_back(r.name); }
    ImGui::SeparatorText("Comparisons by algorithm");
    if (ImPlot::BeginPlot("##sortbars", ImVec2(-1, 240))) {
        ImPlot::SetupAxes("algorithm", "comparisons", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisTicks(ImAxis_X1, 0, static_cast<double>(rows.size() - 1),
                               static_cast<int>(rows.size()), labels.data());
        ImPlot::PlotBars("comparisons", comps.data(), static_cast<int>(comps.size()), 0.6);
        ImPlot::EndPlot();
    }
}

void tabBacktest(App& a) {
    symbolCombo(a);
    const std::vector<double> raw = a.market->candles(a.selectedSymbol(), 60);
    DynamicArray<double> s;
    for (double x : raw) s.push_back(x);
    const auto single = Backtest::bestSingle(s);
    const auto unlim = Backtest::unlimited(s);
    const double k2 = Backtest::maxProfitAtMostK(s, 2);

    ImGui::SeparatorText("Dynamic-programming profit analysis");
    ImGui::Text("Best single trade : %+.2f  (buy day %zu -> sell day %zu)",
                single.profit(), single.buyDay, single.sellDay);
    ImGui::Text("Unlimited trades  : %+.2f  (%zu trades)", unlim.profit, unlim.trades.size());
    ImGui::Text("At most 2 trades  : %+.2f", k2);
    if (ImPlot::BeginPlot("##btchart", ImVec2(-1, 280))) {
        ImPlot::SetupAxes("session", "price", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("close", raw.data(), static_cast<int>(raw.size()));
        std::vector<double> bx, by, sx, sy;
        for (const auto& t : unlim.trades) {
            bx.push_back(static_cast<double>(t.buyDay));  by.push_back(t.buyPrice);
            sx.push_back(static_cast<double>(t.sellDay)); sy.push_back(t.sellPrice);
        }
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, 7, kGreen);
        ImPlot::PlotScatter("buy", bx.data(), by.data(), static_cast<int>(bx.size()));
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Down, 7, kRed);
        ImPlot::PlotScatter("sell", sx.data(), sy.data(), static_cast<int>(sx.size()));
        ImPlot::EndPlot();
    }
}

void tabGraph(App& a) {
    symbolCombo(a);
    ImGui::Text("Vertices: %zu   Edges: %zu", a.graph.vertexCount(), a.graph.edgeCount());
    ImGui::SeparatorText("Related (BFS from selection)");
    const auto order = a.graph.bfs(a.selectedSymbol());
    std::string line;
    for (std::size_t i = 0; i < order.size(); ++i) line += (i ? " -> " : "") + order[i];
    ImGui::TextWrapped("%s", line.c_str());
    ImGui::SeparatorText("Diversification backbone (minimum spanning tree)");
    const auto mst = a.graph.minimumSpanningTree();
    ImGui::Text("Total weight: %.2f", mst.totalWeight);
    if (ImGui::BeginTable("mst", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("A"); ImGui::TableSetupColumn("B");
        ImGui::TableSetupColumn("Weight"); ImGui::TableHeadersRow();
        for (const auto& e : mst.edges) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.a.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.b.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", e.weight);
        }
        ImGui::EndTable();
    }
}

void drawUI(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("PaperTrade", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    accountBar(a);
    ImGui::Spacing();

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Trade"))     { tabTrade(a);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Dashboard")) { tabDashboard(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sort Lab"))  { tabSortLab(a);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Backtest"))  { tabBacktest(a);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Graph"))     { tabGraph(a);     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    // Activity footer
    if (!a.events.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Activity");
        const int show = static_cast<int>(a.events.size() < 3 ? a.events.size() : 3);
        for (int i = 0; i < show; ++i) ImGui::BulletText("%s", a.events[i].c_str());
    }
    ImGui::End();
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6; s.ChildRounding = 6; s.FrameRounding = 5; s.PopupRounding = 5;
    s.GrabRounding = 5; s.TabRounding = 5; s.ScrollbarRounding = 6;
    s.WindowPadding = ImVec2(18, 16); s.FramePadding = ImVec2(11, 7);
    s.ItemSpacing = ImVec2(12, 10); s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = ImVec2(10, 7); s.ScrollbarSize = 14; s.GrabMinSize = 12;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.TabBorderSize = 0;
    s.IndentSpacing = 22;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_Border]            = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.17f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.25f, 0.26f, 0.31f, 1.00f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_Text]              = ImVec4(0.91f, 0.92f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.53f, 0.59f, 1.00f);
    c[ImGuiCol_Button]            = ImVec4(0.20f, 0.44f, 0.86f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.18f, 0.40f, 0.80f, 1.00f);
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.44f, 0.86f, 0.45f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.26f, 0.52f, 0.96f, 0.55f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.26f, 0.52f, 0.96f, 0.75f);
    c[ImGuiCol_Tab]               = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.26f, 0.52f, 0.96f, 0.80f);
    c[ImGuiCol_TabActive]         = ImVec4(0.20f, 0.44f, 0.86f, 1.00f);
    c[ImGuiCol_TabUnfocused]      = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]= ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    c[ImGuiCol_Separator]         = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
}

void glfwError(int code, const char* desc) {
    std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
}

std::unique_ptr<MarketDataService> makeProvider(bool offline) {
    if (offline) return std::make_unique<SyntheticMarketData>();
    util::Env::loadDotEnv();
    const std::string key = util::Env::get("FINNHUB_API_KEY");
    if (!key.empty()) return std::make_unique<FinnhubClient>(key);
    return std::make_unique<LiveMarketData>();
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::strcmp(argv[1], "--smoke") == 0;
    const bool probe = argc > 1 && std::strcmp(argv[1], "--probe") == 0;

    if (probe) {
        auto provider = makeProvider(false);
        const auto quotes = provider->universe();
        std::printf("source: %s\n", provider->sourceName());
        int shown = 0;
        for (const auto& q : quotes) {
            std::printf("  %-6s %10.2f  %+6.2f%%\n", q.symbol.c_str(), q.last, q.pctChange);
            if (++shown >= 6) break;
        }
        return 0;
    }

    std::printf("PaperTrade: loading market data...\n");
    App app(makeProvider(smoke));
    std::printf("PaperTrade: data source = %s\n", app.market->sourceName());
    if (!smoke) app.startRefresh();

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) { std::fprintf(stderr, "Failed to initialise GLFW\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1360, 860, "PaperTrade", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f);  // crisp modern font
    ImGui::StyleColorsDark();
    applyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    int framesLeft = smoke ? 3 : -1;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        app.pumpPrices();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUI(app);
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        if (framesLeft > 0 && --framesLeft == 0) break;
    }

    app.running.store(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (smoke) std::printf("smoke: rendered ok\n");
    return 0;
}
