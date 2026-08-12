//
// main.cpp — PaperTrade desktop application (Dear ImGui + GLFW/OpenGL, ImPlot).
//
// A native brokerage-style GUI over pt_core. Prices stream in live on a
// background thread (one symbol at a time, rate-limit friendly); resting limit
// orders fill automatically when the market crosses the target.
//
// Three screens:
//   Markets   — searchable, sortable quote board + top movers + a stock detail
//               panel (price history, related stocks, historical best trade)
//   Trade     — market/limit order ticket, positions, open orders, history
//   Portfolio — holdings with P&L, sector allocation, recently viewed, watchlist
//
// Data: Finnhub if FINNHUB_API_KEY is in .env, else keyless Yahoo, else
// synthetic — all over WinHTTP. `--smoke` renders headless; `--probe` prints
// live quotes.
//
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/domain/AccountBook.h"
#include "papertrade/domain/Backtest.h"
#include "papertrade/domain/Universe.h"
#include "papertrade/services/FinnhubClient.h"
#include "papertrade/services/LiveMarketData.h"
#include "papertrade/services/Persistence.h"
#include "papertrade/services/SyntheticMarketData.h"
#include "papertrade/structures/MaxHeap.h"
#include "papertrade/structures/MinHeap.h"
#include "papertrade/structures/RecentlyViewed.h"
#include "papertrade/structures/SectorTree.h"
#include "papertrade/structures/StockBST.h"
#include "papertrade/structures/StockGraph.h"
#include "papertrade/structures/sorters/ComparisonSorts.h"
#include "papertrade/util/Env.h"

using namespace papertrade;

namespace {

const ImVec4 kGreen(0.24f, 0.78f, 0.44f, 1.0f);
const ImVec4 kRed(0.94f, 0.38f, 0.38f, 1.0f);
const ImVec4 kMuted(0.60f, 0.63f, 0.69f, 1.0f);
ImVec4 pnlColor(double v) { return v >= 0 ? kGreen : kRed; }

// Where per-user account state is saved between sessions (git-ignored).
const char* kStatePath = "data/state/portfolio.json";

const char* nameOf(const std::string& sym) {
    for (const auto& c : universeList()) if (sym == c.symbol) return c.name;
    return "";
}
const char* sectorOf(const std::string& sym) {
    for (const auto& c : universeList()) if (sym == c.symbol) return c.sector;
    return "Other";
}
std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

SectorTree buildSectorTree() {
    SectorTree t;
    for (const auto& c : universeList()) t.addCompany(c.sector, c.sub, c.symbol);
    return t;
}
StockGraph buildSectorGraph() {
    StockGraph g;
    for (const auto& c : universeList()) g.addVertex(c.symbol);
    const auto& u = universeList();
    for (std::size_t i = 0; i < u.size(); ++i)
        for (std::size_t j = i + 1; j < u.size(); ++j) {
            const bool sameSub = std::strcmp(u[i].sub, u[j].sub) == 0;
            const bool sameSec = std::strcmp(u[i].sector, u[j].sector) == 0;
            if (sameSub) g.addEdge(u[i].symbol, u[j].symbol, 0.2);
            else if (sameSec) g.addEdge(u[i].symbol, u[j].symbol, 0.6);
        }
    return g;
}

// ---- App state -------------------------------------------------------------
struct App {
    std::unique_ptr<MarketDataService> market;
    std::vector<std::string> symbols;   // universe order
    std::vector<Quote> quotes;          // index-aligned with `symbols`
    AccountBook accounts;
    SectorTree sectors = buildSectorTree();
    StockGraph graph = buildSectorGraph();
    RecentlyViewed recent;
    StockBST<std::string, int> symIndex;  // symbol -> index (search / lookup)
    std::string user = "you";

    int selected = 0;
    int orderTypeIdx = 0, sideIdx = 0, tradeQty = 10;
    double limitPrice = 0.0;
    std::string tradeMsg;
    std::vector<std::string> events;
    char search[64] = "";
    std::vector<std::string> watch;
    int rangeIdx = 2;  // 0=1D 1=1W 2=1M 3=1Y 4=5Y
    std::map<std::string, std::vector<Bar>> barCache;
    std::vector<double> eqTimes, eqVals;  // live portfolio-value history

    std::mutex mtx;
    std::vector<Quote> incoming;
    std::atomic<bool> hasFresh{false};
    std::atomic<bool> running{true};
    std::thread refreshThread;
    double lastRefresh = 0.0;

    explicit App(std::unique_ptr<MarketDataService> provider)
        : market(std::move(provider)) {
        for (const auto& c : universeList()) symbols.push_back(c.symbol);
        SyntheticMarketData seed;                 // instant baseline for all symbols
        quotes = seed.universe();                 // aligned to universe order
        for (int i = 0; i < static_cast<int>(symbols.size()); ++i)
            symIndex.insert(symbols[i], i);
        loadPortfolio(accounts.portfolio(user), kStatePath);  // resume saved account
    }
    ~App() {
        running.store(false);
        if (refreshThread.joinable()) refreshThread.join();
        saveState();  // final save on exit
    }

    void saveState() { savePortfolio(accounts.portfolio(user), kStatePath); }

    void startRefresh() {
        refreshThread = std::thread([this] {
            std::vector<Quote> live = quotes;
            std::size_t i = 0;
            while (running.load()) {
                Quote q;
                if (market->quote(symbols[i], q)) live[i] = q;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    incoming = live;
                }
                hasFresh.store(true);
                for (int t = 0; t < 12 && running.load(); ++t)  // ~1.2s between calls
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                i = (i + 1) % symbols.size();
            }
        });
    }

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
            char b[96];
            std::snprintf(b, sizeof(b), "Limit filled: %s %.0f %s @ %.2f",
                          toString(o.side), o.qty, o.ticker.c_str(), o.price);
            events.insert(events.begin(), b);
        }
        if (!filled.empty()) saveState();  // persist auto-executed fills

        // Sample live portfolio value for the tracker chart.
        const double eq = pf.marketValue([this](const std::string& s) { return priceOf(s); });
        eqTimes.push_back(static_cast<double>(std::time(nullptr)));
        eqVals.push_back(eq);
        if (eqVals.size() > 720) {  // keep a rolling window
            eqVals.erase(eqVals.begin());
            eqTimes.erase(eqTimes.begin());
        }
    }

    double priceOf(const std::string& sym) const {
        for (const auto& q : quotes) if (q.symbol == sym) return q.last;
        return 0.0;
    }
    const std::string& selectedSymbol() const { return symbols[selected]; }
    void select(const std::string& sym) {
        if (const int* idx = symIndex.find(sym)) {
            selected = *idx;
            recent.visit(sym);
        }
    }
    bool inWatch(const std::string& s) const {
        for (const auto& w : watch) if (w == s) return true;
        return false;
    }
    void toggleWatch(const std::string& s) {
        for (std::size_t i = 0; i < watch.size(); ++i)
            if (watch[i] == s) { watch.erase(watch.begin() + i); return; }
        watch.push_back(s);
    }
};

// ---- shared bits -----------------------------------------------------------
void pctText(double pct) { ImGui::TextColored(pnlColor(pct), "%+.2f%%", pct); }

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

void moverList(const char* id, const std::vector<Quote>& rows) {
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg)) {
        for (const auto& q : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(q.symbol.c_str());
            ImGui::TableSetColumnIndex(1); pctText(q.pctChange);
        }
        ImGui::EndTable();
    }
}

void symbolCombo(App& a) {
    if (ImGui::BeginCombo("Symbol", a.selectedSymbol().c_str())) {
        for (int i = 0; i < static_cast<int>(a.symbols.size()); ++i) {
            const bool sel = i == a.selected;
            if (ImGui::Selectable(a.symbols[i].c_str(), sel)) a.selected = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// ---- Markets ---------------------------------------------------------------
void marketBoard(App& a) {
    // Filter by search text (symbol or company name).
    const std::string q = toLower(a.search);
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(a.quotes.size()); ++i) {
        if (q.empty() || toLower(a.quotes[i].symbol).find(q) != std::string::npos ||
            toLower(nameOf(a.quotes[i].symbol)).find(q) != std::string::npos)
            rows.push_back(i);
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("board", 5, flags, ImVec2(0, 230))) {
        ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Company");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_NoSort, 0.5f);
        ImGui::TableHeadersRow();

        // Sort the visible rows with our own sorting engine per the chosen column.
        int col = 0; bool asc = true;
        if (ImGuiTableSortSpecs* s = ImGui::TableGetSortSpecs()) {
            if (s->SpecsCount > 0) {
                col = s->Specs[0].ColumnIndex;
                asc = s->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
        }
        DynamicArray<int> idx;
        for (int r : rows) idx.push_back(r);
        auto cmp = [&](const int& x, const int& y) {
            const Quote& qx = a.quotes[x];
            const Quote& qy = a.quotes[y];
            bool less;
            if (col == 2) less = qx.last < qy.last;
            else if (col == 3) less = qx.pctChange < qy.pctChange;
            else if (col == 1) less = std::string(nameOf(qx.symbol)) < nameOf(qy.symbol);
            else less = qx.symbol < qy.symbol;
            return asc ? less : !less;
        };
        QuickSort<int> qs;
        qs.sort(idx, cmp);

        for (std::size_t k = 0; k < idx.size(); ++k) {
            const Quote& qq = a.quotes[idx[k]];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(qq.symbol.c_str(), a.selected == idx[k],
                                  ImGuiSelectableFlags_SpanAllColumns))
                a.select(qq.symbol);
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(kMuted, "%s", nameOf(qq.symbol));
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", qq.last);
            ImGui::TableSetColumnIndex(3); pctText(qq.pctChange);
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(idx[k]);
            if (ImGui::SmallButton(a.inWatch(qq.symbol) ? "-" : "+")) a.toggleWatch(qq.symbol);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// Format a unix timestamp with strftime (e.g. "%a" -> Mon, "%b" -> Jan).
std::string formatEpoch(double t, const char* fmt) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm* tm = std::localtime(&tt);
    char buf[32] = {0};
    if (tm) std::strftime(buf, sizeof(buf), fmt, tm);
    return buf;
}

// Put clean, human-readable ticks on the X (time) axis: weekday names, month
// names, years, etc. — instead of raw numeric dates.
void setupTimeTicks(const std::vector<double>& times, const char* fmt, int maxTicks) {
    if (times.empty()) return;
    const int n = static_cast<int>(times.size());
    const int count = std::min(maxTicks, n);
    static std::vector<double> pos;
    static std::vector<std::string> labs;
    static std::vector<const char*> cptr;
    pos.clear(); labs.clear(); cptr.clear();
    for (int i = 0; i < count; ++i) {
        const int idx = count == 1 ? 0 : i * (n - 1) / (count - 1);
        pos.push_back(times[idx]);
        labs.push_back(formatEpoch(times[idx], fmt));
    }
    for (const auto& s : labs) cptr.push_back(s.c_str());
    ImPlot::SetupAxisTicks(ImAxis_X1, pos.data(), static_cast<int>(pos.size()), cptr.data());
}

// Green/red candlesticks, drawn on the plot's draw list. `fmt` controls the
// X-axis label style for the selected range.
void plotCandles(const std::vector<Bar>& bars, const char* fmt) {
    if (bars.empty()) { ImGui::TextColored(kMuted, "No chart data."); return; }
    double ymin = 1e18, ymax = -1e18;
    for (const auto& b : bars) { if (b.low < ymin) ymin = b.low; if (b.high > ymax) ymax = b.high; }
    const double pad = (ymax - ymin) * 0.06 + 1e-6;
    const double w = bars.size() > 1 ? (bars[1].time - bars[0].time) * 0.34 : 20000.0;

    if (ImPlot::BeginPlot("##candles", ImVec2(-1, 250), ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, "price", 0, 0);
        ImPlot::SetupAxisLimits(ImAxis_X1, bars.front().time - w * 2, bars.back().time + w * 2, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, ymin - pad, ymax + pad, ImPlotCond_Always);
        std::vector<double> ts;
        for (const auto& b : bars) ts.push_back(b.time);
        setupTimeTicks(ts, fmt, 6);
        ImDrawList* dl = ImPlot::GetPlotDrawList();
        const ImU32 up = IM_COL32(62, 207, 142, 255), down = IM_COL32(230, 90, 90, 255);
        for (const auto& b : bars) {
            const ImU32 col = b.close >= b.open ? up : down;
            const ImVec2 wl = ImPlot::PlotToPixels(b.time, b.low);
            const ImVec2 wh = ImPlot::PlotToPixels(b.time, b.high);
            const ImVec2 bl = ImPlot::PlotToPixels(b.time - w, b.open);
            const ImVec2 br = ImPlot::PlotToPixels(b.time + w, b.close);
            dl->AddLine(wl, wh, col, 1.6f);  // high-low wick
            float top = bl.y < br.y ? bl.y : br.y;
            float bot = bl.y < br.y ? br.y : bl.y;
            if (bot - top < 1.0f) bot = top + 1.0f;  // keep flat days visible
            dl->AddRectFilled(ImVec2(bl.x, top), ImVec2(br.x, bot), col);
        }
        ImPlot::EndPlot();
    }
}

void stockDetail(App& a) {
    const std::string sym = a.selectedSymbol();
    ImGui::Text("%s", sym.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%s  ·  %s", nameOf(sym), sectorOf(sym));
    ImGui::SameLine();
    ImGui::Text("   %.2f", a.priceOf(sym));

    // Time-range selector: 1D / 1W / 1M / 1Y / 5Y.
    static const char* rlabels[5] = {"1D", "1W", "1M", "1Y", "5Y"};
    static const char* rrange[5] = {"1d", "5d", "1mo", "1y", "5y"};
    static const char* rint[5] = {"15m", "1d", "1d", "1wk", "1mo"};
    static const char* rfmt[5] = {"%H:%M", "%a", "%b %d", "%b", "%Y"};
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine();
        const bool active = a.rangeIdx == i;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.44f, 0.86f, 1));
        if (ImGui::SmallButton(rlabels[i])) a.rangeIdx = i;
        if (active) ImGui::PopStyleColor();
    }

    // Fetch+cache OHLC bars for this symbol & range (one network call per key).
    const std::string key =
        sym + "|" + rrange[a.rangeIdx] + "|" + rint[a.rangeIdx];
    auto it = a.barCache.find(key);
    if (it == a.barCache.end())
        it = a.barCache.emplace(key, a.market->bars(sym, rrange[a.rangeIdx], rint[a.rangeIdx])).first;
    const std::vector<Bar>& bars = it->second;
    plotCandles(bars, rfmt[a.rangeIdx]);

    // Related stocks (graph neighbours).
    ImGui::TextColored(kMuted, "Related:");
    ImGui::SameLine();
    const auto rel = a.graph.neighbors(sym);
    if (rel.empty()) ImGui::TextDisabled("none");
    for (std::size_t i = 0; i < rel.size() && i < 8; ++i) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(rel[i].c_str())) a.select(rel[i]);
        ImGui::PopID();
    }

    // Historical best trade (subtle insight over the period).
    DynamicArray<double> d;
    for (const auto& b : bars) d.push_back(b.close);
    const auto best = Backtest::bestSingle(d);
    if (best.profit() > 0)
        ImGui::TextColored(kMuted, "Best move this period: %+.1f%% (buy low, sell high)",
                           best.buyPrice != 0 ? best.profit() / best.buyPrice * 100 : 0);
}

void tabMarkets(App& a) {
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##search", "Search symbol or company...", a.search, sizeof(a.search));
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%d companies", static_cast<int>(a.symbols.size()));

    ImGui::Columns(2, "mkt", true);
    marketBoard(a);
    ImGui::NextColumn();
    ImGui::SeparatorText("Top Gainers");
    moverList("gain", topMovers(a.quotes, 6, true));
    ImGui::SeparatorText("Top Losers");
    moverList("lose", topMovers(a.quotes, 6, false));
    ImGui::Columns(1);

    ImGui::SeparatorText("Details");
    stockDetail(a);
}

// ---- Trade -----------------------------------------------------------------
void orderTicket(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const std::string sym = a.selectedSymbol();
    const double last = a.priceOf(sym);

    ImGui::Text("%s", sym.c_str());
    ImGui::SameLine(); ImGui::TextColored(kMuted, " %s  ·  last %.2f", nameOf(sym), last);

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
            r = side == Side::Buy ? pf.buy(sym, a.tradeQty, last) : pf.sell(sym, a.tradeQty, last);
        else
            r = pf.placeLimit(sym, side, a.tradeQty, a.limitPrice);
        a.tradeMsg = r.message;
        if (r.ok) {
            char b[96];
            std::snprintf(b, sizeof(b), "%s %s %d %s%s", r.message.c_str(), toString(side),
                          a.tradeQty, sym.c_str(), a.orderTypeIdx == 1 ? " (resting)" : "");
            a.events.insert(a.events.begin(), b);
            a.saveState();
        }
    }
    if (!a.tradeMsg.empty()) ImGui::TextColored(kMuted, "%s", a.tradeMsg.c_str());
}

void positionsTable(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    if (ImGui::BeginTable("pos", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol"); ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Avg cost"); ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("Mkt value"); ImGui::TableSetupColumn("Unreal P&L");
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
    if (pf.pendingCount() == 0) { ImGui::TextColored(kMuted, "No resting limit orders."); return; }
    if (ImGui::BeginTable("open", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Side"); ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty"); ImGui::TableSetupColumn("Limit");
        ImGui::TableSetupColumn(""); ImGui::TableHeadersRow();
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
        if (cancelId >= 0) { pf.cancelPending(cancelId); a.saveState(); }
    }
}

void historyTable(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const std::size_t n = pf.orderCount();
    if (n == 0) { ImGui::TextColored(kMuted, "No orders yet."); return; }
    if (ImGui::BeginTable("hist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Side"); ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty"); ImGui::TableSetupColumn("Price");
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
    ImGui::SeparatorText("Order ticket");
    symbolCombo(a);
    orderTicket(a);
    if (ImGui::Button("Undo last trade")) {
        if (a.accounts.portfolio(a.user).undoLast()) { a.tradeMsg = "Undid last trade"; a.saveState(); }
    }
    ImGui::NextColumn();
    ImGui::SeparatorText("Open orders");
    openOrdersTable(a);
    ImGui::SeparatorText("Recent orders");
    historyTable(a);
    ImGui::Columns(1);

    ImGui::SeparatorText("Positions");
    positionsTable(a);
}

// ---- Portfolio -------------------------------------------------------------
void sectorAllocation(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    std::map<std::string, double> alloc;
    double total = 0;
    for (const auto& q : a.quotes)
        if (const Position* p = pf.position(q.symbol)) {
            const double v = p->qty * q.last;
            alloc[sectorOf(q.symbol)] += v;
            total += v;
        }
    if (total <= 0) { ImGui::TextColored(kMuted, "Buy something to see your allocation."); return; }
    if (ImGui::BeginTable("alloc", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Sector"); ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Weight"); ImGui::TableHeadersRow();
        for (const auto& kv : alloc) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(kv.first.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", kv.second);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f%%", kv.second / total * 100);
        }
        ImGui::EndTable();
    }
}

void recentlyViewed(App& a) {
    std::vector<std::string> seen;
    a.recent.forEachReverse([&](const std::string& s) {
        for (const auto& x : seen) if (x == s) return;
        if (seen.size() < 10) seen.push_back(s);
    });
    if (seen.empty()) { ImGui::TextColored(kMuted, "Nothing viewed yet."); return; }
    for (const auto& s : seen) {
        ImGui::PushID(s.c_str());
        if (ImGui::SmallButton(s.c_str())) a.select(s);
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%.2f", a.priceOf(s));
    }
}

void watchlistPanel(App& a) {
    if (a.watch.empty()) { ImGui::TextColored(kMuted, "Add symbols with + on the Markets tab."); return; }
    if (ImGui::BeginTable("watch", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol"); ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg"); ImGui::TableHeadersRow();
        for (const auto& s : a.watch) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(s.c_str())) a.select(s);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", a.priceOf(s));
            ImGui::TableSetColumnIndex(2);
            for (const auto& q : a.quotes) if (q.symbol == s) { pctText(q.pctChange); break; }
        }
        ImGui::EndTable();
    }
}

// Live portfolio-value line — green when up on the session, red when down.
void portfolioTracker(App& a) {
    if (a.eqVals.size() < 2) {
        ImGui::TextColored(kMuted, "Live tracker fills in as prices update — buy some stock to watch it move.");
        return;
    }
    const double first = a.eqVals.front(), last = a.eqVals.back();
    const bool up = last >= first;
    const ImVec4 col = up ? kGreen : kRed;
    const double chg = last - first, pct = first != 0 ? chg / first * 100 : 0;
    ImGui::Text("Portfolio value  %.2f", last);
    ImGui::SameLine();
    ImGui::TextColored(col, "   %+.2f (%+.2f%%) this session", chg, pct);
    if (ImPlot::BeginPlot("##track", ImVec2(-1, 230), ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, "value", 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, a.eqTimes.front(), a.eqTimes.back(), ImPlotCond_Always);
        setupTimeTicks(a.eqTimes, "%H:%M", 6);
        const int n = static_cast<int>(a.eqVals.size());
        ImPlot::SetNextFillStyle(col, 0.12f);
        ImPlot::PlotShaded("value", a.eqTimes.data(), a.eqVals.data(), n, first);
        ImPlot::SetNextLineStyle(col, 2.2f);
        ImPlot::PlotLine("value", a.eqTimes.data(), a.eqVals.data(), n);
        ImPlot::EndPlot();
    }
}

void tabPortfolio(App& a) {
    ImGui::BeginChild("pfscroll", ImVec2(0, 0), false);
    ImGui::SeparatorText("Live portfolio value");
    portfolioTracker(a);
    ImGui::SeparatorText("Positions");
    positionsTable(a);
    ImGui::Columns(2, "pf", true);
    ImGui::SeparatorText("Sector allocation");
    sectorAllocation(a);
    ImGui::NextColumn();
    ImGui::SeparatorText("Watchlist");
    watchlistPanel(a);
    ImGui::Columns(1);
    ImGui::SeparatorText("Recently viewed");
    recentlyViewed(a);
    ImGui::EndChild();
}

// ---- chrome ----------------------------------------------------------------
void accountBar(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);
    const double equity = pf.marketValue([&](const std::string& s) { return a.priceOf(s); });
    double unreal = 0.0;
    for (const auto& q : a.quotes)
        if (const Position* p = pf.position(q.symbol)) unreal += (q.last - p->avgCost) * p->qty;

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
        if (ImGui::BeginTabItem("Markets"))   { tabMarkets(a);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Trade"))     { tabTrade(a);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Portfolio")) { tabPortfolio(a); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
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
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.TabBorderSize = 0; s.IndentSpacing = 22;
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
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    c[ImGuiCol_Separator]         = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.26f, 0.52f, 0.96f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
}

void glfwError(int code, const char* desc) { std::fprintf(stderr, "[glfw] %d: %s\n", code, desc); }

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
        std::printf("source: %s\n", provider->sourceName());
        int shown = 0;
        for (const auto& c : universeList()) {
            Quote q;
            if (provider->quote(c.symbol, q))
                std::printf("  %-6s %10.2f  %+6.2f%%\n", q.symbol.c_str(), q.last, q.pctChange);
            if (++shown >= 6) break;
        }
        return 0;
    }

    std::printf("PaperTrade: loading...\n");
    App app(makeProvider(smoke));
    std::printf("PaperTrade: data source = %s\n", app.market->sourceName());
    if (!smoke) app.startRefresh();

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) { std::fprintf(stderr, "Failed to init GLFW\n"); return 1; }
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
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f);
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
