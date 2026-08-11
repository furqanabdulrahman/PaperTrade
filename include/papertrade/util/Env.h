#pragma once
//
// Env.h — tiny .env loader + environment-variable accessor.
//
// This is INFRASTRUCTURE code, not a graded data structure, so it is allowed
// to use std::unordered_map / std::string / std::ifstream freely (see spec §1.2).
// It exists so the Finnhub API key is never hardcoded (spec §1.5): values are
// read from the process environment first, then from a git-ignored `.env` file.
//
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace papertrade::util {

class Env {
public:
    // Load key=value pairs from a .env file if it exists. Missing file is fine
    // (returns false); process environment variables still take precedence.
    static bool loadDotEnv(const std::string& path = ".env") {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            const auto eq = trimmed.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(trimmed.substr(0, eq));
            std::string value = trim(trimmed.substr(eq + 1));
            // Strip optional surrounding quotes on the value.
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }
            store()[key] = value;
        }
        return true;
    }

    // Look up a value: real environment variable wins, then the loaded .env,
    // then the provided fallback.
    static std::string get(const std::string& key,
                           const std::string& fallback = "") {
        if (const char* sys = std::getenv(key.c_str()); sys != nullptr) {
            return std::string(sys);
        }
        const auto it = store().find(key);
        if (it != store().end()) return it->second;
        return fallback;
    }

    static int getInt(const std::string& key, int fallback) {
        const std::string v = get(key);
        if (v.empty()) return fallback;
        try {
            return std::stoi(v);
        } catch (...) {
            return fallback;
        }
    }

private:
    static std::unordered_map<std::string, std::string>& store() {
        static std::unordered_map<std::string, std::string> s;
        return s;
    }

    static std::string trim(const std::string& s) {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return "";
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }
};

}  // namespace papertrade::util
