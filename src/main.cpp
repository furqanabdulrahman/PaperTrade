//
// main.cpp — PaperTrade backend entrypoint.
//
// Phase 1 scope: load .env, stand up the cpp-httplib server, expose GET /health,
// and serve the built Vite frontend as static files. Later phases register their
// own route handlers here via the api/ layer.
//
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "papertrade/util/Env.h"

using json = nlohmann::json;
using papertrade::util::Env;

namespace {
std::atomic<bool> g_running{true};

httplib::Server* g_server = nullptr;

void handleSignal(int) {
    g_running = false;
    if (g_server) g_server->stop();
}
}  // namespace

int main(int argc, char** argv) {
    // Load config: real env vars take precedence over the git-ignored .env file.
    Env::loadDotEnv(".env");

    const int port = Env::getInt("PAPERTRADE_PORT", 8080);
    const std::string webRoot = "web/dist";

    httplib::Server server;
    g_server = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // --- Liveness probe -----------------------------------------------------
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        const json body = {
            {"status", "ok"},
            {"service", "papertrade"},
            {"phase", 1},
        };
        res.set_content(body.dump(), "application/json");
    });

    // --- Static frontend ----------------------------------------------------
    // Serve the built SPA if it exists. If the frontend hasn't been built yet,
    // the mount point is simply absent and only the API responds.
    if (!server.set_mount_point("/", webRoot)) {
        std::cerr << "[warn] no static frontend at '" << webRoot
                  << "' (run `npm run build` in web/) — API-only mode\n";
    }

    // SPA fallback: any non-API, non-file GET returns index.html so client-side
    // routing works on deep links. Only wired when the build exists.
    server.set_error_handler([webRoot](const httplib::Request& req,
                                       httplib::Response& res) {
        if (res.status == 404 && req.method == "GET" &&
            req.path.rfind("/api", 0) != 0 && req.path != "/health") {
            std::ifstream index(webRoot + "/index.html", std::ios::binary);
            if (index) {
                std::stringstream ss;
                ss << index.rdbuf();
                res.set_content(ss.str(), "text/html");
                res.status = 200;
            }
        }
    });

    std::cout << "PaperTrade backend listening on http://localhost:" << port
              << "  (GET /health)\n";

    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "[fatal] failed to bind port " << port << "\n";
        return 1;
    }
    return 0;
}
