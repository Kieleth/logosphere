// CurlTransport — libcurl-based ILLMTransport implementation.
//
// Speaks HTTPS (TLS, cert verification, redirects, SNI). Used for
// cloud LLM APIs (Anthropic, OpenAI). For plain-TCP localhost
// endpoints (mlx_lm.server, llama-server) HTTPClient (TcpTransport) is faster + has
// zero deps.
//
// Compiled in when LOGOSPHERE_LLM_HTTPS=ON in CMake (default ON
// if find_package(CURL) succeeds).

#pragma once

#include "logosphere/llm/llm_transport.h"

#include <string>

namespace Logosphere {

class CurlTransport : public ILLMTransport {
public:
    CurlTransport(const std::string& host, int port, bool use_https);
    ~CurlTransport() override;

    HTTPResponse post(const std::string& path,
                      const std::string& body,
                      const std::string& content_type,
                      const ExtraHeaders& extra_headers) override;

    bool is_reachable() const override;

private:
    std::string host_;
    int         port_;
    bool        use_https_;
    std::string base_url_;  // "https://host:port" or "http://..."
};

}  // namespace Logosphere
