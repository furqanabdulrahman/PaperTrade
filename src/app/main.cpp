//
// main.cpp — PaperTrade desktop application (Dear ImGui + GLFW/OpenGL, ImPlot).
//
// A native GUI over pt_core. Every tab is driven by the real graded structures:
//   Dashboard  — MaxHeap/MinHeap movers, SectorTree browser, ImPlot price chart
//   Portfolio  — Portfolio/AccountBook order engine (buy/sell/undo, avg cost)
//   Sort Lab   — all 8 Sorter<T> strategies, live comparison/move instrumentation
//   Backtest   — DP max-profit (bestSingle / unlimited / at-most-k) with markers
//   Graph      — StockGraph BFS + minimum spanning tree over a sector graph
// Market data comes from a MarketDataService (synthetic today, Finnhub when the
// SSL build is enabled). `--smoke` renders a few frames headless and exits.
//
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "papertrade/adt/DynamicArray.h"
#include "papertrade/domain/AccountBook.h"
#include "papertrade/domain/Backtest.h"
#include "papertrade/services/SyntheticMarketData.h"
#include "papertrade/structures/MaxHeap.h"
#include "papertrade/structures/MinHeap.h"
#include "papertrade/structures/SectorTree.h"
#include "papertrade/structures/StockGraph.h"
#include "papertrade/structures/sorters/ComparisonSorts.h"
#include "papertrade/structures/sorters/NonComparisonSorts.h"

using namespace papertrade;

namespace {

// ---- Static sector taxonomy for the demo universe --------------------------
struct SectorOf {
    const char* sym;
    const char* sector;
    const char* sub;
};
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

// A sector graph: edge between every pair sharing a sub-sector (weight 0.2) or
// sector (weight 0.6) — a stand-in for a correlation graph.
StockGraph buildSectorGraph() {
    StockGraph g;
    for (const auto& e : kTaxonomy) g.addVertex(e.sym);
    const int n = static_cast<int>(sizeof(kTaxonomy) / sizeof(kTaxonomy[0]));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const bool sameSub = std::strcmp(kTaxonomy[i].sub, kTaxonomy[j].sub) == 0;
            const bool sameSec = std::strcmp(kTaxonomy[i].sector, kTaxonomy[j].sector) == 0;
            if (sameSub)
                g.addEdge(kTaxonomy[i].sym, kTaxonomy[j].sym, 0.2);
            else if (sameSec)
                g.addEdge(kTaxonomy[i].sym, kTaxonomy[j].sym, 0.6);
        }
    return g;
}

// ---- App state -------------------------------------------------------------
struct App {
    SyntheticMarketData market;
    std::vector<Quote> quotes;
    AccountBook accounts;
    SectorTree sectors = buildSectorTree();
    StockGraph graph = buildSectorGraph();
    std::string user = "you";
    int selected = 0;   // index into quotes
    int tradeQty = 10;
    std::string tradeMsg;

    // Sort Lab
    std::vector<int> sortSeed;
    int sortSize = 400;

    App() {
        quotes = market.universe();
        regenSortData();
    }

    void regenSortData() {
        sortSeed.clear();
        std::uint64_t s = 88172645463325252ULL + static_cast<std::uint64_t>(sortSize);
        for (int i = 0; i < sortSize; ++i) {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
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

void moverTable(const char* id, const std::vector<Quote>& rows) {
    if (ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg");
        ImGui::TableHeadersRow();
        for (const auto& q : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(q.symbol.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", q.last);
            ImGui::TableSetColumnIndex(2);
            const ImVec4 c = q.pctChange >= 0 ? ImVec4(0.30f, 0.80f, 0.44f, 1)
                                              : ImVec4(0.90f, 0.36f, 0.36f, 1);
            ImGui::TextColored(c, "%+.2f%%", q.pctChange);
        }
        ImGui::EndTable();
    }
}

void drawSectorNode(const SectorTree::SectorNode* n) {
    if (n->leaf()) {
        ImGui::BulletText("%s", n->name.c_str());
        return;
    }
    if (ImGui::TreeNodeEx(n->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))  {
        for (const auto& c : n->children) drawSectorNode(c.get());
        ImGui::TreePop();
    }
}

// Symbol picker shared by several tabs.
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

// ---- Tabs ------------------------------------------------------------------
void tabDashboard(App& a) {
    ImGui::Columns(2, "dash", true);
    ImGui::SeparatorText("Top Gainers");
    moverTable("g", topMovers(a.quotes, 5, true));
    ImGui::SeparatorText("Top Losers");
    moverTable("l", topMovers(a.quotes, 5, false));

    ImGui::Spacing();
    symbolCombo(a);
    const std::vector<double> series = a.market.candles(a.selectedSymbol(), 40);
    ImGui::SeparatorText((a.selectedSymbol() + " — 40-session close").c_str());
    if (ImPlot::BeginPlot("##dashchart", ImVec2(-1, 220))) {
        ImPlot::SetupAxes("session", "price", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("close", series.data(), static_cast<int>(series.size()));
        ImPlot::EndPlot();
    }

    ImGui::NextColumn();
    ImGui::SeparatorText("Sectors");
    for (const auto& c : a.sectors.root()->children) drawSectorNode(c.get());
    ImGui::Columns(1);
}

void tabPortfolio(App& a) {
    Portfolio& pf = a.accounts.portfolio(a.user);

    ImGui::SeparatorText("Trade");
    symbolCombo(a);
    ImGui::InputInt("Quantity", &a.tradeQty);
    if (a.tradeQty < 1) a.tradeQty = 1;
    const double px = a.priceOf(a.selectedSymbol());
    ImGui::Text("Price: %.2f    Est. cost: %.2f", px, px * a.tradeQty);

    if (ImGui::Button("Buy")) {
        auto r = pf.buy(a.selectedSymbol(), a.tradeQty, px);
        a.tradeMsg = (r.ok ? "Bought " : "Rejected: ") + (r.ok ? a.selectedSymbol() : r.message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Sell")) {
        auto r = pf.sell(a.selectedSymbol(), a.tradeQty, px);
        a.tradeMsg = (r.ok ? "Sold " : "Rejected: ") + (r.ok ? a.selectedSymbol() : r.message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo last")) a.tradeMsg = pf.undoLast() ? "Undid last trade" : "Nothing to undo";
    if (!a.tradeMsg.empty()) ImGui::TextDisabled("%s", a.tradeMsg.c_str());

    const double mv = pf.marketValue([&](const std::string& s) { return a.priceOf(s); });
    ImGui::SeparatorText("Account");
    ImGui::Text("Cash: %.2f    Market value: %.2f    Orders: %zu", pf.cash(), mv,
                pf.orderCount());

    ImGui::SeparatorText("Holdings");
    if (ImGui::BeginTable("hold", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Qty");
        ImGui::TableSetupColumn("Avg cost");
        ImGui::TableSetupColumn("Mkt value");
        ImGui::TableHeadersRow();
        for (const auto& q : a.quotes) {
            const Position* p = pf.position(q.symbol);
            if (!p) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(q.symbol.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.0f", p->qty);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", p->avgCost);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", p->qty * q.last);
        }
        ImGui::EndTable();
    }
}

void tabSortLab(App& a) {
    ImGui::SeparatorText("Sorting benchmark");
    ImGui::InputInt("Array size", &a.sortSize);
    if (a.sortSize < 2) a.sortSize = 2;
    if (a.sortSize > 5000) a.sortSize = 5000;
    ImGui::SameLine();
    if (ImGui::Button("Regenerate")) a.regenSortData();
    ImGui::TextDisabled("Same random array fed to every algorithm; counters reset per run.");

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
        ImGui::TableSetupColumn("Algorithm");
        ImGui::TableSetupColumn("Comparisons");
        ImGui::TableSetupColumn("Moves");
        ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.name);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", r.comps);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", r.moves);
        }
        ImGui::EndTable();
    }

    std::vector<double> comps;
    std::vector<const char*> labels;
    for (const auto& r : rows) { comps.push_back(static_cast<double>(r.comps)); labels.push_back(r.name); }
    ImGui::SeparatorText("Comparisons by algorithm");
    if (ImPlot::BeginPlot("##sortbars", ImVec2(-1, 220))) {
        ImPlot::SetupAxes("algorithm", "comparisons", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisTicks(ImAxis_X1, 0, static_cast<double>(rows.size() - 1),
                               static_cast<int>(rows.size()), labels.data());
        ImPlot::PlotBars("comparisons", comps.data(), static_cast<int>(comps.size()), 0.6);
        ImPlot::EndPlot();
    }
}

void tabBacktest(App& a) {
    symbolCombo(a);
    const std::vector<double> raw = a.market.candles(a.selectedSymbol(), 60);
    DynamicArray<double> series;
    for (double x : raw) series.push_back(x);

    const auto single = Backtest::bestSingle(series);
    const auto unlim = Backtest::unlimited(series);
    const double k2 = Backtest::maxProfitAtMostK(series, 2);

    ImGui::SeparatorText("Dynamic-programming profit analysis (60 sessions)");
    ImGui::Text("Best single trade : %+.2f  (buy day %zu -> sell day %zu)",
                single.profit(), single.buyDay, single.sellDay);
    ImGui::Text("Unlimited trades  : %+.2f  (%zu trades)", unlim.profit, unlim.trades.size());
    ImGui::Text("At most 2 trades  : %+.2f", k2);

    if (ImPlot::BeginPlot("##btchart", ImVec2(-1, 260))) {
        ImPlot::SetupAxes("session", "price", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("close", raw.data(), static_cast<int>(raw.size()));
        std::vector<double> bx, by, sx, sy;
        for (const auto& t : unlim.trades) {
            bx.push_back(static_cast<double>(t.buyDay));  by.push_back(t.buyPrice);
            sx.push_back(static_cast<double>(t.sellDay)); sy.push_back(t.sellPrice);
        }
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, 7, ImVec4(0.30f, 0.80f, 0.44f, 1));
        ImPlot::PlotScatter("buy", bx.data(), by.data(), static_cast<int>(bx.size()));
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Down, 7, ImVec4(0.90f, 0.36f, 0.36f, 1));
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
        ImGui::TableSetupColumn("A");
        ImGui::TableSetupColumn("B");
        ImGui::TableSetupColumn("Weight");
        ImGui::TableHeadersRow();
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
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginMenuBar()) {
        ImGui::TextDisabled("PaperTrade");
        ImGui::Separator();
        ImGui::TextDisabled("data: %s", a.market.sourceName());
        ImGui::EndMenuBar();
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Dashboard")) { tabDashboard(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Portfolio")) { tabPortfolio(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sort Lab"))  { tabSortLab(a);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Backtest"))  { tabBacktest(a);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Graph"))     { tabGraph(a);     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void glfwError(int code, const char* desc) {
    std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::strcmp(argv[1], "--smoke") == 0;

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "PaperTrade", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    App app;

    int framesLeft = smoke ? 3 : -1;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI(app);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        if (framesLeft > 0 && --framesLeft == 0) break;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (smoke) std::printf("smoke: rendered ok\n");
    return 0;
}
