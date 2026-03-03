#include "common/http_client.h"
#include <fstream>

// ═══════════════════════════════════════════════════════════════════════════
// Backend 1: libcurl (Linux / native builds with curl installed)
// ═══════════════════════════════════════════════════════════════════════════

#if defined(HAS_CURL)

#include <curl/curl.h>

bool http_is_available() { return true; }
void http_global_init()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup() { curl_global_cleanup(); }

static size_t write_string_cb(void* ptr, size_t size, size_t nmemb, void* ud) {
    auto* buf = static_cast<std::string*>(ud);
    buf->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

static size_t write_file_cb(void* ptr, size_t size, size_t nmemb, void* ud) {
    auto* f = static_cast<std::ofstream*>(ud);
    f->write(static_cast<char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

static int progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* p = static_cast<std::atomic<float>*>(clientp);
    if (dltotal > 0)
        p->store(static_cast<float>(dlnow) / static_cast<float>(dltotal));
    return 0;
}

HttpResponse http_get(const std::string& url,
                      const std::vector<std::string>& headers,
                      long timeout_sec) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.error_msg = "Failed to initialize HTTP client"; return resp; }

    struct curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = curl_slist_append(hlist, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) resp.error_msg = curl_easy_strerror(res);
    return resp;
}

HttpResponse http_get_file(const std::string& url,
                           const std::string& output_path,
                           const std::vector<std::string>& headers,
                           std::atomic<float>* progress,
                           long timeout_sec) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.error_msg = "Failed to initialize HTTP client"; return resp; }

    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) {
        curl_easy_cleanup(curl);
        resp.error_msg = "Failed to create local file";
        return resp;
    }

    struct curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = curl_slist_append(hlist, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    file.close();

    if (res != CURLE_OK) resp.error_msg = curl_easy_strerror(res);
    return resp;
}

HttpResponse http_put(const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& headers,
                      long timeout_sec) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.error_msg = "Failed to initialize HTTP client"; return resp; }

    struct curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = curl_slist_append(hlist, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) resp.error_msg = curl_easy_strerror(res);
    return resp;
}

// ═══════════════════════════════════════════════════════════════════════════
// Backend 2: WinHTTP (Windows — no external dependencies)
// ═══════════════════════════════════════════════════════════════════════════

#elif defined(HAS_WINHTTP)

#include <windows.h>
#include <winhttp.h>
#include <cstring>

bool http_is_available() { return true; }
void http_global_init()  {}   // WinHTTP needs no global init
void http_global_cleanup() {}

// ── Helpers ─────────────────────────────────────────────────────────────

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), len);
    return ws;
}

struct UrlParts {
    bool           https = true;
    std::wstring   host;
    INTERNET_PORT  port  = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring   path;   // includes query string
};

static bool parse_url(const std::string& url, UrlParts& out) {
    std::wstring wurl = to_wide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host_buf[256]{};
    wchar_t path_buf[4096]{};
    uc.lpszHostName      = host_buf;
    uc.dwHostNameLength  = 256;
    uc.lpszUrlPath       = path_buf;
    uc.dwUrlPathLength   = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        return false;

    out.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out.host  = host_buf;
    out.port  = uc.nPort;
    out.path  = path_buf;
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        out.path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return true;
}

// RAII guard for WinHTTP handles
struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

// ── Core request ────────────────────────────────────────────────────────

static HttpResponse winhttp_perform(const std::string& method,
                                     const std::string& url,
                                     const std::vector<std::string>& headers,
                                     const std::string& body,
                                     const std::string& output_file,
                                     std::atomic<float>* progress,
                                     long timeout_sec) {
    HttpResponse resp;

    UrlParts parts;
    if (!parse_url(url, parts)) {
        resp.error_msg = "Failed to parse URL";
        return resp;
    }

    WinHttpHandle session{WinHttpOpen(L"ParticlePlayground/1.0",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) { resp.error_msg = "WinHttpOpen failed"; return resp; }

    DWORD timeout_ms = static_cast<DWORD>(timeout_sec) * 1000;
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    WinHttpHandle connect{WinHttpConnect(session, parts.host.c_str(), parts.port, 0)};
    if (!connect) { resp.error_msg = "WinHttpConnect failed"; return resp; }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wmethod = to_wide(method);
    WinHttpHandle request{WinHttpOpenRequest(connect, wmethod.c_str(),
                                              parts.path.c_str(),
                                              nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request) { resp.error_msg = "WinHttpOpenRequest failed"; return resp; }

    // Follow redirects
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    // Add headers
    for (const auto& h : headers) {
        std::wstring wh = to_wide(h);
        WinHttpAddRequestHeaders(request, wh.c_str(), (DWORD)wh.size(),
                                  WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Send
    LPVOID body_ptr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str();
    DWORD  body_len = body.empty() ? 0 : (DWORD)body.size();

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             body_ptr, body_len, body_len, 0)) {
        resp.error_msg = "WinHttpSendRequest failed";
        return resp;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        resp.error_msg = "WinHttpReceiveResponse failed";
        return resp;
    }

    // Status code
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(request,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                         WINHTTP_NO_HEADER_INDEX);
    resp.status_code = static_cast<long>(status);

    // Content-Length for progress
    DWORD content_length = 0;
    sz = sizeof(content_length);
    bool have_cl = WinHttpQueryHeaders(request,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &sz,
        WINHTTP_NO_HEADER_INDEX) != 0;

    // Read body
    bool to_file = !output_file.empty();
    std::ofstream file;
    if (to_file) {
        file.open(output_file, std::ios::binary);
        if (!file.is_open()) {
            resp.error_msg = "Failed to create local file";
            return resp;
        }
    }

    DWORD total_read = 0, bytes_avail = 0;
    while (WinHttpQueryDataAvailable(request, &bytes_avail) && bytes_avail > 0) {
        std::vector<char> buf(bytes_avail);
        DWORD bytes_read = 0;
        if (WinHttpReadData(request, buf.data(), bytes_avail, &bytes_read)) {
            if (to_file) file.write(buf.data(), bytes_read);
            else         resp.body.append(buf.data(), bytes_read);
            total_read += bytes_read;
            if (progress && have_cl && content_length > 0)
                progress->store(static_cast<float>(total_read) /
                                static_cast<float>(content_length));
        }
    }
    if (to_file) file.close();

    return resp;
}

// ── Public API ──────────────────────────────────────────────────────────

HttpResponse http_get(const std::string& url,
                      const std::vector<std::string>& headers,
                      long timeout_sec) {
    return winhttp_perform("GET", url, headers, "", "", nullptr, timeout_sec);
}

HttpResponse http_get_file(const std::string& url,
                           const std::string& output_path,
                           const std::vector<std::string>& headers,
                           std::atomic<float>* progress,
                           long timeout_sec) {
    return winhttp_perform("GET", url, headers, "", output_path, progress, timeout_sec);
}

HttpResponse http_put(const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& headers,
                      long timeout_sec) {
    return winhttp_perform("PUT", url, headers, body, "", nullptr, timeout_sec);
}

// ═══════════════════════════════════════════════════════════════════════════
// No HTTP backend available
// ═══════════════════════════════════════════════════════════════════════════

#else

bool http_is_available() { return false; }
void http_global_init()  {}
void http_global_cleanup() {}

HttpResponse http_get(const std::string&, const std::vector<std::string>&, long) {
    return {0, "", "No HTTP backend available"};
}
HttpResponse http_get_file(const std::string&, const std::string&,
                           const std::vector<std::string>&,
                           std::atomic<float>*, long) {
    return {0, "", "No HTTP backend available"};
}
HttpResponse http_put(const std::string&, const std::string&,
                      const std::vector<std::string>&, long) {
    return {0, "", "No HTTP backend available"};
}

#endif
