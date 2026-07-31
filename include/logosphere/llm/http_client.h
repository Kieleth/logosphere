// Logosphere Engine — Plain-TCP HTTP/1.1 client.
//
// Implements ILLMTransport (see llm_transport.h). Used by
// LLMSystemHTTP for localhost OpenAI-compatible endpoints (mlx_lm.server,
// llama-server, Ollama). Plain http://, no
// TLS, zero external dependencies (just POSIX sockets). For HTTPS,
// see CurlTransport (built when LOGOSPHERE_LLM_HTTPS is on).

#pragma once

#include "logosphere/llm/llm_transport.h"  // HTTPResponse, ExtraHeaders, ILLMTransport

#include <string>

namespace Logosphere {

class HTTPClient : public ILLMTransport {
public:
    HTTPClient(const std::string& host, int port);
    ~HTTPClient() override;

    // ILLMTransport
    HTTPResponse post(const std::string& path,
                      const std::string& body,
                      const std::string& content_type,
                      const ExtraHeaders& extra_headers) override;
    bool is_reachable() const override { return is_connected(); }

    // Convenience overload preserving the v0.9 signature so
    // existing call sites that don't need extra headers don't
    // have to pass {}.
    HTTPResponse post(const std::string& path,
                      const std::string& body,
                      const std::string& content_type = "application/json") {
        return post(path, body, content_type, ExtraHeaders{});
    }

    // Plain TCP reachability probe (just-connect). Kept as the
    // public API name HTTPClient exposed before ILLMTransport.
    bool is_connected() const;

private:
    std::string host_;
    int port_;

    // Low-level socket operations
    int connect_socket();
    void close_socket(int sockfd);
    bool send_data(int sockfd, const std::string& data);
    std::string recv_data(int sockfd);

    // HTTP protocol helpers
    std::string build_request(const std::string& method,
                              const std::string& path,
                              const std::string& body,
                              const std::string& content_type,
                              const ExtraHeaders& extra_headers);
    HTTPResponse parse_response(const std::string& raw_response);
};

} // namespace Logosphere
