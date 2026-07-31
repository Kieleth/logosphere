// Logosphere Engine - Minimal HTTP Client Implementation
//
// PURPOSE: Enable LLM tool calling without external dependencies (no libcurl, no Boost.Asio)
//
// DESIGN PHILOSOPHY:
// - Zero external dependencies: Uses POSIX sockets directly (macOS/Linux standard)
// - Localhost-only: Connects to local OpenAI-compatible LLM server
//   (mlx_lm.server on Mac, llama.cpp's llama-server, Ollama, etc.)
// - No TLS needed: Local connections on 127.0.0.1:8000
// - Simple HTTP/1.1: Just enough to POST JSON and receive responses
// - Blocking I/O: Fine for game loop (LLM calls are infrequent, 1-2 per second max)
//
// TOOL CALLING FLOW:
// 1. LLMSystemHTTP builds JSON request with tools + user prompt
// 2. HTTPClient::post() → MLX server at localhost:8000
// 3. MLX processes prompt, returns tool call as JSON in response
// 4. LLMSystemHTTP parses tool call, executes via ToolRegistry
// 5. Repeat if tool result needs further processing (ReAct loop, max 10 iterations)
//
// PERFORMANCE CHARACTERISTICS:
// - Connection: ~1ms (localhost socket)
// - Request send: ~1ms for typical 1-2KB JSON payload
// - LLM inference: 50-100ms (MLX on M4 Max, depends on prompt length)
// - Response receive: ~5-10ms (streaming recv until Content-Length satisfied)
// - Total: ~60-120ms per tool call (dominated by LLM inference, not HTTP)
//
// OPTIMIZATION OPPORTUNITIES:
// 1. Connection pooling: Reuse socket instead of connect/close each request (~1ms saved)
// 2. HTTP pipelining: Not beneficial (LLM inference dominates, not network)
// 3. Async I/O: Would complicate code, gain minimal (game loop already async)
// 4. Reduce logging: std::cerr calls add ~1-2ms, could make conditional
// 5. Batch tool calls: Execute multiple tools in parallel (requires LLM changes)
//
// CURRENT BOTTLENECKS (measured 2025-01-12):
// - LLM inference: 50-100ms (85% of total time) ← Main bottleneck
// - Socket recv loop: 5-10ms (body chunking overhead)
// - JSON parsing: 2-3ms (nlohmann::json is fast enough)
// - Socket setup: 1-2ms (acceptable for infrequent calls)
//
// WHY NOT USE LIBRARIES?
// - libcurl: 2MB+ dependency, overkill for localhost POST
// - Boost.Asio: Header-only but complex, not needed for simple blocking I/O
// - cpr/cpp-httplib: Extra dependencies, more code than our 200-line implementation
// - HTTP keeps it simple: Entire client is 260 lines, easy to debug and maintain
//
// STABILITY NOTES:
// - 120s timeout: LLM inference can be slow for complex prompts (typical: 50-200ms)
// - Content-Length required: MLX-LM always send it, parser relies on it
// - Connection: close: New socket per request (simple, avoids keep-alive issues)
// - Error handling: Socket errors return immediately with error response

#include "logosphere/llm/http_client.h"
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <cerrno>
#include <string>

namespace Logosphere {

HTTPClient::HTTPClient(const std::string& host, int port)
    : host_(host)
    , port_(port)
{
}

HTTPClient::~HTTPClient() {
    // Nothing to cleanup (no persistent connection)
}

bool HTTPClient::is_connected() const {
    // Try to connect (lightweight check)
    int sockfd = const_cast<HTTPClient*>(this)->connect_socket();
    if (sockfd < 0) return false;
    const_cast<HTTPClient*>(this)->close_socket(sockfd);
    return true;
}

int HTTPClient::connect_socket() {
    // PERF: This is called once per LLM request (~1-2ms overhead)
    // OPTIMIZATION: Could maintain persistent connection to save 1ms per request
    // TRADE-OFF: Persistent connection adds complexity (keepalive, reconnection logic)
    // DECISION: Current approach is simpler and 1ms is negligible vs 50-100ms LLM time

    // Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Set 120s timeout for both send and receive
    // WHY: LLM inference can take 50-200ms typically, but complex prompts may take longer
    // SAFETY: Prevents indefinite hang if MLX server crashes or stalls
    struct timeval timeout;
    timeout.tv_sec = 120;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Disable Nagle's algorithm to ensure data is sent immediately
    // This prevents TCP from buffering small packets
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // =========================================================================
    // CRITICAL FIX: SO_NOSIGPIPE on macOS
    // =========================================================================
    //
    // ROOT CAUSE (discovered 2025-01-19):
    // When HTTPClient was called from worker threads (e.g., LLMSystemHTTP's
    // worker thread) after engine initialization, socket send() would buffer
    // data indefinitely instead of transmitting it. The server would only
    // receive the data when the socket was closed (at timeout), causing
    // 120-second timeouts with errno=35 (EAGAIN).
    //
    // SYMPTOMS:
    // - Standalone HTTP test worked instantly
    // - Same code from worker thread after engine init timed out
    // - Server logs showed request arriving at exact timeout moment
    // - send() returned success but data was buffered for 60-120 seconds
    //
    // WHY THIS HAPPENS:
    // On macOS, when a socket write would cause SIGPIPE (e.g., if the remote
    // end closes), the default behavior can interfere with socket operations
    // in multi-threaded contexts. The engine creates 14+ worker threads for
    // rendering, and SIGPIPE signal handling interacts poorly with socket
    // buffering in these threads.
    //
    // THE FIX:
    // SO_NOSIGPIPE tells the kernel to return EPIPE error instead of sending
    // SIGPIPE signal. This ensures proper socket behavior regardless of thread
    // context or signal handler state inherited from parent threads.
    //
    // On Linux, use MSG_NOSIGNAL flag on send() instead (not needed here as
    // this codebase targets macOS).
    //
    // VERIFICATION:
    // - curl/netcat with same HTTP request: worked instantly
    // - Standalone HTTPClient test (no engine): worked instantly
    // - HTTPClient from engine worker thread without SO_NOSIGPIPE: 120s timeout
    // - HTTPClient from engine worker thread with SO_NOSIGPIPE: works instantly
    //
    // =========================================================================
#ifdef __APPLE__
    setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
#endif

    // Setup server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    // Convert hostname to IP (supports "127.0.0.1" or "localhost")
    // NOTE: No DNS lookup needed, localhost always resolves to 127.0.0.1
    if (host_ == "localhost") {
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    } else {
        server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
    }

    // Connect to server (blocking call, returns when connection established or fails)
    // PERF: ~1ms for localhost, would be slower for remote hosts
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void HTTPClient::close_socket(int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

bool HTTPClient::send_data(int sockfd, const std::string& data) {
    size_t total_sent = 0;
    size_t data_len = data.length();

    while (total_sent < data_len) {
        ssize_t sent = send(sockfd, data.c_str() + total_sent, data_len - total_sent, 0);
        if (sent < 0) {
            return false;
        }
        total_sent += sent;
    }

    return true;
}

std::string HTTPClient::recv_data(int sockfd) {
    // PERF: This is the slowest part of HTTP client (~5-10ms for typical 1-2KB response)
    // BOTTLENECK: String concatenation in loop, Content-Length parsing on every iteration
    // OPTIMIZATION OPPORTUNITIES:
    // 1. Reserve string capacity upfront if Content-Length parsed early (~2ms saved)
    // 2. Parse Content-Length once, not on every recv iteration (~1-2ms saved)
    // 3. Use larger buffer (8KB instead of 4KB) for fewer recv calls (~1ms saved)
    // 4. Conditional logging (remove std::cerr in production) (~1-2ms saved)
    // TOTAL POTENTIAL: ~5-7ms speedup (reduces HTTP overhead from 10ms to 3-5ms)

    std::string result;
    char buffer[4096];  // PERF: Could increase to 8192 for fewer recv() calls

    while (true) {
        // Blocking recv: waits until data available or timeout (120s)
        ssize_t received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        if (received < 0) {
            // Socket error or timeout (120s expired)
            std::cerr << "[HTTPClient] recv error: errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
            break;
        } else if (received == 0) {
            // Server closed connection (normal for "Connection: close")
            std::cerr << "[HTTPClient] Connection closed by server" << std::endl;
            break;
        }

        // Append received data to result
        buffer[received] = '\0';
        result.append(buffer, received);  // PERF: Allocates on every append if not reserved

        // Check if we received complete response
        // HTTP response format: headers + "\r\n\r\n" + body
        if (result.find("\r\n\r\n") != std::string::npos) {
            // PERF: This block runs on EVERY recv iteration after headers received
            // OPTIMIZATION: Parse Content-Length once, store it, check only body_length

            // Check if we have Content-Length header (required by MLX-LM)
            size_t cl_pos = result.find("Content-Length:");
            if (cl_pos == std::string::npos) {
                cl_pos = result.find("content-length:");  // Try lowercase (HTTP is case-insensitive)
            }

            if (cl_pos != std::string::npos) {
                // Parse Content-Length value
                size_t cl_end = result.find("\r\n", cl_pos);
                std::string cl_str = result.substr(cl_pos + 15, cl_end - cl_pos - 15);

                // Trim whitespace (some servers add spaces)
                size_t first = cl_str.find_first_not_of(" \t");
                size_t last = cl_str.find_last_not_of(" \t");
                if (first != std::string::npos) {
                    cl_str = cl_str.substr(first, last - first + 1);
                }

                int content_length = std::stoi(cl_str);
                std::cerr << "[HTTPClient] Content-Length: " << content_length << std::endl;

                // Find where body starts (after "\r\n\r\n")
                size_t body_start = result.find("\r\n\r\n") + 4;
                int body_length = result.length() - body_start;
                std::cerr << "[HTTPClient] Body received: " << body_length << "/" << content_length << " bytes" << std::endl;

                // Check if we received complete body
                if (body_length >= content_length) {
                    break;  // Success: full response received
                }
                // Otherwise, continue receiving
            } else {
                // No Content-Length found (shouldn't happen with MLX-LM)
                // Assume response is complete and exit
                std::cerr << "[HTTPClient] No Content-Length found, assuming complete" << std::endl;
                break;
            }
        }
    }

    std::cerr << "[HTTPClient] Total received: " << result.length() << " bytes" << std::endl;
    return result;
}

std::string HTTPClient::build_request(const std::string& method,
                                       const std::string& path,
                                       const std::string& body,
                                       const std::string& content_type,
                                       const ExtraHeaders& extra_headers) {
    std::ostringstream request;

    // Request line
    request << method << " " << path << " HTTP/1.1\r\n";

    // Headers - include port in Host header (required by HTTP/1.1 for non-standard ports)
    request << "Host: " << host_ << ":" << port_ << "\r\n";
    request << "Content-Type: " << content_type << "\r\n";
    request << "Content-Length: " << body.length() << "\r\n";
    request << "Connection: close\r\n";

    // Per-provider auth + custom headers, threaded through from
    // LLMSystemHTTP (Bearer / x-api-key / etc.). No validation —
    // the LLM client builds the right ones for each provider.
    for (const auto& [name, value] : extra_headers) {
        request << name << ": " << value << "\r\n";
    }

    request << "\r\n";

    // Body
    request << body;

    return request.str();
}

HTTPResponse HTTPClient::parse_response(const std::string& raw_response) {
    HTTPResponse response;
    response.error = false;

    if (raw_response.empty()) {
        response.error = true;
        response.error_message = "Empty response";
        response.status_code = 0;
        return response;
    }

    // Parse status line
    size_t status_line_end = raw_response.find("\r\n");
    if (status_line_end == std::string::npos) {
        response.error = true;
        response.error_message = "Invalid HTTP response";
        response.status_code = 0;
        return response;
    }

    std::string status_line = raw_response.substr(0, status_line_end);

    // Extract status code (e.g., "HTTP/1.1 200 OK" → 200)
    size_t code_start = status_line.find(" ") + 1;
    size_t code_end = status_line.find(" ", code_start);
    std::string code_str = status_line.substr(code_start, code_end - code_start);
    response.status_code = std::stoi(code_str);

    // Extract body (after "\r\n\r\n")
    size_t body_start = raw_response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        response.body = raw_response.substr(body_start + 4);
    }

    return response;
}

HTTPResponse HTTPClient::post(const std::string& path,
                               const std::string& body,
                               const std::string& content_type,
                               const ExtraHeaders& extra_headers) {
    HTTPResponse response;

    // Start timing the entire request
    auto request_start = std::chrono::steady_clock::now();

    std::cerr << "[HTTPClient] POST " << host_ << ":" << port_ << path << std::endl;
    std::cerr << "[HTTPClient] Body size: " << body.length() << " bytes" << std::endl;

    // Connect
    auto connect_start = std::chrono::steady_clock::now();
    int sockfd = connect_socket();
    auto connect_end = std::chrono::steady_clock::now();
    auto connect_ms = std::chrono::duration_cast<std::chrono::milliseconds>(connect_end - connect_start).count();

    if (sockfd < 0) {
        std::cerr << "[HTTPClient] ERROR: Failed to connect" << std::endl;
        response.error = true;
        response.error_message = "Failed to connect to server";
        response.status_code = 0;
        return response;
    }
    std::cerr << "[HTTPClient] Connected successfully (fd=" << sockfd << ") in " << connect_ms << "ms" << std::endl;

    // Build and send request
    auto send_start = std::chrono::steady_clock::now();
    std::string request = build_request("POST", path, body, content_type, extra_headers);
    std::cerr << "[HTTPClient] Request size: " << request.length() << " bytes" << std::endl;
    std::cerr << "[HTTPClient] Request headers:\n" << request.substr(0, request.find("\r\n\r\n") + 4) << std::endl;

    if (!send_data(sockfd, request)) {
        std::cerr << "[HTTPClient] ERROR: Failed to send request" << std::endl;
        response.error = true;
        response.error_message = "Failed to send request";
        response.status_code = 0;
        close_socket(sockfd);
        return response;
    }
    auto send_end = std::chrono::steady_clock::now();
    auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - send_start).count();
    std::cerr << "[HTTPClient] Request sent successfully in " << send_ms << "ms" << std::endl;

    // Receive response (THIS IS WHERE LLM INFERENCE HAPPENS - can take several seconds!)
    std::cerr << "[HTTPClient] Waiting for response (LLM inference time)..." << std::endl;
    auto recv_start = std::chrono::steady_clock::now();
    std::string raw_response = recv_data(sockfd);
    auto recv_end = std::chrono::steady_clock::now();
    auto recv_ms = std::chrono::duration_cast<std::chrono::milliseconds>(recv_end - recv_start).count();
    close_socket(sockfd);

    std::cerr << "[HTTPClient] Response received in " << recv_ms << "ms (LLM inference time)" << std::endl;
    std::cerr << "[HTTPClient] Response received, parsing..." << std::endl;
    std::cerr << "[HTTPClient] Raw response (" << raw_response.length() << " bytes): "
              << raw_response.substr(0, 800) << (raw_response.length() > 800 ? "..." : "") << std::endl;
    // Parse response
    response = parse_response(raw_response);

    // Total request time
    auto request_end = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(request_end - request_start).count();
    std::cerr << "[HTTPClient] ⏱️  TOTAL HTTP REQUEST TIME: " << total_ms << "ms "
              << "(connect: " << connect_ms << "ms, send: " << send_ms << "ms, "
              << "LLM inference: " << recv_ms << "ms)" << std::endl;

    return response;
}

} // namespace Logosphere
