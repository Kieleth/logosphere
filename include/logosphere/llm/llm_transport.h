// Pluggable HTTP transport for LLM systems. Two impls ship today:
//
//   TcpTransport   — POSIX sockets, plain HTTP/1.1. Zero deps.
//                    Use for localhost OpenAI-compatible endpoints
//                    (mlx_lm.server, llama-server, Ollama).
//                    Lives in HTTPClient (back-compat name).
//
//   CurlTransport  — libcurl wrapper, HTTPS + TLS + cert verify +
//                    redirects. Compiled in when LOGOSPHERE_LLM_HTTPS
//                    is on (default ON if libcurl is found by CMake).
//                    Use for cloud APIs (Anthropic, OpenAI).
//
// LLMSystemHTTP picks one at initialize() time based on the URL
// scheme (http:// → TCP, https:// → curl). Provider-specific JSON
// shaping (request body, response field paths, tool-call wrapping)
// stays in LLMSystemHTTP — the transport is dumb, the LLM client
// is smart.
//
// Engine-side. Generic — any caller that wants HTTP+TLS without
// pulling in libcurl directly can use this.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Logosphere {

// Per-request extra HTTP headers (in addition to Host /
// Content-Type / Content-Length / Connection that every transport
// sets itself). Used to thread per-provider auth (Bearer for
// OpenAI, x-api-key for Anthropic, etc.) without leaking provider
// knowledge into the transport.
using ExtraHeaders = std::vector<std::pair<std::string, std::string>>;

// Minimal HTTP response. Shared with all ILLMTransport impls.
struct HTTPResponse {
    int         status_code = 0;
    std::string body;
    bool        error = false;
    std::string error_message;
};

class ILLMTransport {
public:
    virtual ~ILLMTransport() = default;

    // Send a POST. Synchronous; callers needing async wrap with
    // their own worker thread (LLMSystemHTTP already does this).
    virtual HTTPResponse post(const std::string& path,
                              const std::string& body,
                              const std::string& content_type,
                              const ExtraHeaders& extra_headers) = 0;

    // Lightweight reachability probe. Implementations are free to
    // do a TCP connect / TLS handshake / HEAD request — anything
    // that confirms the endpoint is alive. Used by
    // LLMSystemHTTP::initialize to fail fast.
    virtual bool is_reachable() const = 0;
};

}  // namespace Logosphere
