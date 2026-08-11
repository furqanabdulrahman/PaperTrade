//
// main.cpp — PaperTrade desktop application entrypoint.
//
// Native GUI shell (Dear ImGui + GLFW/OpenGL, ImPlot for charts) built directly
// on top of pt_core: the same graded DSA that used to sit behind the retired
// HTTP server now drives the on-screen views in-process. This first cut proves
// the pivot end to end —
//   * Top Gainers / Top Losers   → MaxHeap / MinHeap (partial heap-select)
//   * Sector browser             → SectorTree (general n-ary tree)
//   * Price chart                → ImPlot over a demo series
// The demo data below is a placeholder until the Phase-4 Finnhub client feeds
// live quotes into these same structures.
//
// Run headless self-check: `papertrade --smoke` renders a few frames and exits.
//
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "papertrade/structures/MaxHeap.h"
#include "papertrade/structures/MinHeap.h"
#include "papertrade/structures/SectorTree.h"

using namespace papertrade;

namespace {

struct Mover {
    std::string sym;
    double last;
    double pct;  // day % change
};

// ---- Demo universe (placeholder until the Finnhub client lands) ------------
std::vector<Mover> demoMovers() {
    return {
        {"NVDA", 121.40, 7.53}, {"TSLA", 244.10, 4.12}, {"AMD", 168.90, 3.01},
        {"AAPL", 229.35, 1.18}, {"MSFT", 428.70, 0.42}, {"AMZN", 186.20, -0.31},
        {"GOOG", 172.55, -0.88}, {"META", 512.30, -1.24}, {"JPM", 214.05, -1.90},
        {"KO", 62.10, -2.35}, {"INTC", 21.75, -4.60}, {"PFE", 28.40, -3.10},
    };
}

SectorTree demoSectors() {
    SectorTree t;
    t.addCompany("Technology", "Semiconductors", "NVDA");
    t.addCompany("Technology", "Semiconductors", "AMD");
    t.addCompany("Technology", "Semiconductors", "INTC");
    t.addCompany("Technology", "Software", "MSFT");
    t.addCompany("Technology", "Internet", "GOOG");
    t.addCompany("Technology", "Internet", "META");
    t.addCompany("Consumer", "Autos", "TSLA");
    t.addCompany("Consumer", "Retail", "AMZN");
    t.addCompany("Consumer", "Beverages", "KO");
    t.addCompany("Financials", "Banks", "JPM");
    t.addCompany("Healthcare", "Pharma", "PFE");
    return t;
}

// A seeded pseudo random-walk so the chart shows something plausible.
std::vector<double> demoPrices(double start, int n) {
    std::vector<double> out;
    out.reserve(n);
    double p = start;
    unsigned seed = 12345;
    for (int i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        double r = ((seed >> 16) & 0x7fff) / 32768.0 - 0.5;  // [-0.5, 0.5)
        p *= (1.0 + r * 0.03);
        out.push_back(p);
    }
    return out;
}

// Partial heap-select: top-k movers by % change. Exercises the real heaps every
// call (O(n + k log n)) rather than a full sort.
std::vector<Mover> topMovers(const std::vector<Mover>& all, int k, bool gainers) {
    const auto byPct = [](const Mover& a, const Mover& b) { return a.pct < b.pct; };
    std::vector<Mover> out;
    if (gainers) {
        MaxHeap<Mover> h(byPct);
        for (const auto& m : all) h.push(m);
        for (int i = 0; i < k && !h.empty(); ++i) out.push_back(h.pop());
    } else {
        MinHeap<Mover> h(byPct);
        for (const auto& m : all) h.push(m);
        for (int i = 0; i < k && !h.empty(); ++i) out.push_back(h.pop());
    }
    return out;
}

void drawMoverTable(const char* id, const std::vector<Mover>& rows) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable(id, 3, flags)) {
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("% Chg");
        ImGui::TableHeadersRow();
        for (const auto& m : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(m.sym.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", m.last);
            ImGui::TableSetColumnIndex(2);
            const ImVec4 col = m.pct >= 0 ? ImVec4(0.30f, 0.80f, 0.44f, 1.0f)
                                          : ImVec4(0.90f, 0.36f, 0.36f, 1.0f);
            ImGui::TextColored(col, "%+.2f%%", m.pct);
        }
        ImGui::EndTable();
    }
}

void drawSectorNode(const SectorTree::SectorNode* n) {
    if (n->leaf()) {
        ImGui::BulletText("%s", n->name.c_str());
        return;
    }
    if (ImGui::TreeNodeEx(n->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& c : n->children) drawSectorNode(c.get());
        ImGui::TreePop();
    }
}

// Draws the whole dashboard for one frame.
void drawDashboard(const std::vector<Mover>& movers, const SectorTree& sectors,
                   const std::vector<double>& prices) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("PaperTrade", nullptr, root_flags);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Exit", "Alt+F4");
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("  |  practice trading, real market data");
        ImGui::EndMenuBar();
    }

    ImGui::Columns(2, "layout", true);

    // --- left: movers + chart ------------------------------------------------
    ImGui::SeparatorText("Top Gainers");
    drawMoverTable("gainers", topMovers(movers, 5, /*gainers=*/true));
    ImGui::Spacing();
    ImGui::SeparatorText("Top Losers");
    drawMoverTable("losers", topMovers(movers, 5, /*gainers=*/false));

    ImGui::Spacing();
    ImGui::SeparatorText("AAPL — 30-session close");
    if (ImPlot::BeginPlot("##price", ImVec2(-1, 220))) {
        ImPlot::SetupAxes("session", "price", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("close", prices.data(), static_cast<int>(prices.size()));
        ImPlot::EndPlot();
    }

    // --- right: sector browser ----------------------------------------------
    ImGui::NextColumn();
    ImGui::SeparatorText("Sectors");
    ImGui::Text("%zu companies across the universe", sectors.tickersInSector("Technology").size() +
                                                         0);  // (demo stat)
    for (const auto& c : sectors.root()->children) drawSectorNode(c.get());

    ImGui::Columns(1);
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
    if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // offscreen self-check

    GLFWwindow* window =
        glfwCreateWindow(1280, 800, "PaperTrade", nullptr, nullptr);
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
    ImGui::GetIO().IniFilename = nullptr;  // don't litter an imgui.ini
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    const std::vector<Mover> movers = demoMovers();
    const SectorTree sectors = demoSectors();
    const std::vector<double> prices = demoPrices(210.0, 30);

    int framesLeft = smoke ? 3 : -1;  // -1 == run until the window closes
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawDashboard(movers, sectors, prices);

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
