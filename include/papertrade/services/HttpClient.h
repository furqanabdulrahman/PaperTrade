#pragma once
//
// HttpClient.h — minimal HTTPS GET over the platform's native TLS.
//
// On Windows this is backed by WinHTTP (Schannel), so live market data needs NO
// OpenSSL and NO 64-bit toolchain change — it works on the existing MinGW setup.
// Infrastructure code, not a graded structure.
//
#include <string>

namespace papertrade {

struct HttpResponse {
    int status = 0;
    std::string body;
    bool ok() const { return status == 200; }
};

// GET https://<host><path>. `host` is bare (e.g. "query1.finance.yahoo.com"),
// `path` begins with '/' and may carry a query string. Returns status 0 on a
// transport failure.
HttpResponse httpsGet(const std::string& host, const std::string& path);

}  // namespace papertrade
