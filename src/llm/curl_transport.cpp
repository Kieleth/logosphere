#include "logosphere/llm/curl_transport.h"

#include <curl/curl.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

namespace Logosphere {

namespace {

// One-shot global init, ref-counted by user count. libcurl's
// curl_global_init must be called once per process before any
// easy/multi handles are created. cleanup happens at process exit
// (we don't bother with curl_global_cleanup; harmless on shutdown).
std::once_flag g_curl_init_once;
void ensure_curl_initialized() {
    std::call_once(g_curl_init_once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

// libcurl write callback — appends received bytes to a std::string.
size_t append_to_string(void* contents, size_t size, size_t nmemb,
                        void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

}  // namespace

CurlTransport::CurlTransport(const std::string& host, int port,
                             bool use_https)
    : host_(host), port_(port), use_https_(use_https) {
    ensure_curl_initialized();

    // Default ports per scheme — let the URL parser drop them
    // when they match (Anthropic / OpenAI dislike explicit :443
    // in some routes).
    bool default_port = (use_https_ && port_ == 443) ||
                        (!use_https_ && port_ == 80);
    base_url_ = (use_https_ ? "https://" : "http://") + host_;
    if (!default_port) {
        base_url_ += ":" + std::to_string(port_);
    }
}

CurlTransport::~CurlTransport() = default;

HTTPResponse CurlTransport::post(const std::string& path,
                                 const std::string& body,
                                 const std::string& content_type,
                                 const ExtraHeaders& extra_headers) {
    HTTPResponse out;

    CURL* curl = curl_easy_init();
    if (!curl) {
        out.error = true;
        out.error_message = "curl_easy_init failed";
        return out;
    }

    const std::string url = base_url_ + path;

    curl_easy_setopt(curl, CURLOPT_URL,                url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST,               1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,         body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,      static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,     1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,           1L);   // safe in worker threads
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,            120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,     30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,          "logosphere-llm/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,      append_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,          &out.body);
    if (use_https_) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }

    // Headers — Content-Type first, then extras (auth, anthropic-
    // version, etc.). libcurl owns the slist; we free it after.
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers,
        ("Content-Type: " + content_type).c_str());
    for (const auto& [name, value] : extra_headers) {
        headers = curl_slist_append(headers, (name + ": " + value).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        out.error = true;
        out.error_message = std::string("curl: ") + curl_easy_strerror(rc);
    } else {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        out.status_code = static_cast<int>(status);
        if (out.status_code < 200 || out.status_code >= 300) {
            out.error = true;
            out.error_message = "HTTP " + std::to_string(out.status_code);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return out;
}

bool CurlTransport::is_reachable() const {
    // Cheapest probe libcurl supports: a HEAD against the base
    // URL with a short timeout. CONNECT_ONLY is also available
    // but doesn't validate TLS handshake. HEAD does.
    ensure_curl_initialized();
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL,             base_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY,          1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,        1L);
    if (use_https_) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    // Cloud APIs return 4xx for HEAD on /; that's still "reachable".
    // Any non-CURLE_OK is treated as unreachable (DNS, TLS, etc.).
    return rc == CURLE_OK;
}

}  // namespace Logosphere
