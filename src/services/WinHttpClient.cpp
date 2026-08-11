//
// WinHttpClient.cpp — httpsGet() implemented with WinHTTP (Windows Schannel TLS).
// No OpenSSL, no extra libraries beyond the OS. See HttpClient.h.
//
#include "papertrade/services/HttpClient.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <string>

namespace papertrade {

namespace {
std::wstring widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());  // ASCII hosts/paths only
}
}  // namespace

HttpResponse httpsGet(const std::string& host, const std::string& path) {
    HttpResponse out;

    HINTERNET session = WinHttpOpen(L"Mozilla/5.0 PaperTrade",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return out;
    WinHttpSetTimeouts(session, 4000, 4000, 6000, 6000);

    HINTERNET connect =
        WinHttpConnect(session, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect) {
        HINTERNET request = WinHttpOpenRequest(
            connect, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request) {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD code = 0, codeLen = sizeof(code);
                WinHttpQueryHeaders(
                    request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen, WINHTTP_NO_HEADER_INDEX);
                out.status = static_cast<int>(code);

                DWORD avail = 0;
                do {
                    avail = 0;
                    if (!WinHttpQueryDataAvailable(request, &avail)) break;
                    if (avail == 0) break;
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, &chunk[0], avail, &read)) break;
                    chunk.resize(read);
                    out.body += chunk;
                } while (avail > 0);
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return out;
}

}  // namespace papertrade

#else  // non-Windows: no native client wired up

namespace papertrade {
HttpResponse httpsGet(const std::string&, const std::string&) { return {}; }
}  // namespace papertrade

#endif
