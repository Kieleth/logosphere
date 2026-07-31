// Logosphere Engine - LLM System HTTP Backend
//
// Purpose: Generic LLM integration via HTTP APIs
// Design: Works with MLX-LM (Apple Silicon Metal), OpenAI, Anthropic,
// or any OpenAI-compatible HTTP API. Note: vLLM was the original local
// target but has no Metal backend; on Mac use mlx_lm.server instead.
// The "MLX" provider speaks the same /v1/chat/completions schema, so
// the same code path also works with Ollama, llama.cpp's llama-server,
// LM Studio, or actual vLLM if you happen to have a CUDA box.
// Benefits: Complete GPU isolation, flexible provider choice

#pragma once

#include <string>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <memory>

// Include shared types from llm_system.h (must be before namespace)
#include "logosphere/llm/llm_system.h"
#include "logosphere/llm/llm_transport.h"  // ExtraHeaders, HTTPResponse

// Forward declaration
class ToolRegistry;

namespace Logosphere {

// Forward decls — full defs in their own headers, included by .cpp.
class HTTPClient;
class CurlTransport;

// ============================================================================
// LLM Provider Types
// ============================================================================

enum class LLMProvider {
    MLX,         // Local MLX server (OpenAI-compatible)
    OpenAI,       // OpenAI API (requires API key)
    Anthropic,    // Anthropic API (requires API key)
    Custom        // Custom provider (manual JSON format)
};

// Provider configuration
struct LLMProviderConfig {
    LLMProvider provider = LLMProvider::MLX;
    std::string endpoint_url;       // e.g., "http://localhost:8000" or "https://api.openai.com/v1"
    std::string api_key;            // Optional, for OpenAI/Anthropic
    std::string model_name;         // e.g., "Qwen/Qwen2.5-14B-Instruct", "gpt-4", "claude-3-5-sonnet-20241022"
    std::string completion_path;    // e.g., "/v1/completions", "/v1/chat/completions", "/v1/messages"
};

// ============================================================================
// LLMSystemHTTP - Generic HTTP-based LLM backend
// ============================================================================

class LLMSystemHTTP {
public:
    LLMSystemHTTP();
    ~LLMSystemHTTP();

    // ========================================================================
    // INITIALIZATION
    // ========================================================================

    // Initialize with provider configuration
    bool initialize(const LLMProviderConfig& config);

    // Convenience initializers for common providers
    bool initialize_mlx(const std::string& url, const std::string& model);
    bool initialize_openai(const std::string& api_key, const std::string& model);
    bool initialize_anthropic(const std::string& api_key, const std::string& model);

    void shutdown();
    bool is_initialized() const { return initialized_; }
    std::string get_last_error() const { return last_error_; }

    // ========================================================================
    // TOOL CALLING
    // ========================================================================

    void register_tool_registry(ToolRegistry* registry);

    // ========================================================================
    // ASYNC API (same interface as LLMSystem)
    // ========================================================================

    uint64_t submit_request(const std::string& prompt,
                           int max_tokens,
                           LLMCallback callback,
                           void* user_data = nullptr);

    void cancel_request(uint64_t request_id);
    bool process_completed_responses();
    bool has_pending_requests() const;
    int get_queue_size() const;

    // ========================================================================
    // TRACING
    // ========================================================================

    void set_trace_config(const LLMTraceConfig& config);

    // ========================================================================
    // NARRATIVE MODE (DungeonMaster-style Q&A with scene context)
    // ========================================================================

    // Set system prompt for narrative mode (non-root42 requests)
    void set_narrative_system_prompt(const std::string& prompt);

    // Set callback to get current scene context for narrative mode
    void set_scene_context_callback(SceneContextCallback callback);

    // Warmup: Send system prompt to LLM to prime KV cache (async, fire-and-forget)
    void warmup_narrative();

    // ========================================================================
    // SYNC API (blocking, backward compatibility)
    // ========================================================================

    std::string generate(const std::string& prompt, int max_tokens = 50);

private:
    // ========================================================================
    // HTTP client and provider config
    // ========================================================================
    std::unique_ptr<ILLMTransport> http_client_;
    LLMProviderConfig config_;

    // ========================================================================
    // State
    // ========================================================================
    bool initialized_;
    std::string last_error_;

    // ========================================================================
    // Tool calling
    // ========================================================================
    ToolRegistry* tool_registry_;

    // ========================================================================
    // Tracing
    // ========================================================================
    LLMTraceConfig trace_config_;

    // ========================================================================
    // Narrative mode (DungeonMaster-style Q&A)
    // ========================================================================
    std::string narrative_system_prompt_;  // Loaded from file at startup
    SceneContextCallback scene_context_callback_;  // Returns current scene state

    // ========================================================================
    // Worker thread (same pattern as LLMSystem)
    // ========================================================================
    std::thread worker_thread_;
    std::atomic<bool> shutdown_requested_{false};
    void worker_thread_main();

    // ========================================================================
    // Thread-safe queues
    // ========================================================================
    mutable std::mutex request_mutex_;
    std::queue<LLMRequest> request_queue_;
    std::condition_variable request_cv_;

    mutable std::mutex response_mutex_;
    std::queue<LLMResponse> response_queue_;

    // ========================================================================
    // Cancellation
    // ========================================================================
    mutable std::mutex cancel_mutex_;
    std::unordered_set<uint64_t> cancelled_requests_;

    // ========================================================================
    // Request ID generation
    // ========================================================================
    std::atomic<uint64_t> next_request_id_{1};

    // ========================================================================
    // Provider-specific JSON formatting
    // ========================================================================

    // Per-provider auth + custom headers (Bearer for OpenAI,
    // x-api-key + anthropic-version for Anthropic, none for MLX-LM).
    // Computed once at init from config_; reused on every post.
    Logosphere::ExtraHeaders auth_headers_;
    void rebuild_auth_headers_();

    // Build request JSON for specific provider
    std::string build_request_json(const std::string& prompt, int max_tokens);

    // Parse response JSON from specific provider
    std::string parse_response_json(const std::string& json_response);

    // Provider-specific builders (single message)
    std::string build_mlx_request(const std::string& prompt, int max_tokens);
    std::string build_openai_request(const std::string& prompt, int max_tokens);
    std::string build_anthropic_request(const std::string& prompt, int max_tokens);

    // Provider-specific builders (multi-message: system + user)
    std::string build_mlx_request_with_system(const std::string& system_prompt,
                                                 const std::string& user_prompt,
                                                 int max_tokens);
    std::string build_openai_request_with_system(const std::string& system_prompt,
                                                   const std::string& user_prompt,
                                                   int max_tokens);
    std::string build_anthropic_request_with_system(const std::string& system_prompt,
                                                      const std::string& user_prompt,
                                                      int max_tokens);

    // Provider-specific builders (message array for KV cache optimization)
    std::string build_mlx_request_with_messages(const std::vector<Message>& messages, int max_tokens);
    std::string build_openai_request_with_messages(const std::vector<Message>& messages, int max_tokens);
    std::string build_anthropic_request_with_messages(const std::vector<Message>& messages, int max_tokens);

    // Provider-specific parsers
    std::string parse_mlx_response(const std::string& json);
    std::string parse_openai_response(const std::string& json);
    std::string parse_anthropic_response(const std::string& json);

    // ========================================================================
    // Internal generation
    // ========================================================================
    std::string generate_internal(const std::string& prompt, int max_tokens);

public:
    // ========================================================================
    // JSON helpers (minimal, no library). Public static: pure
    // utilities, unit-tested in tests/test_llm_json_extract.cpp.
    // ========================================================================
    static std::string json_escape(const std::string& str);
    // Extract the string value of "key" from a JSON document and
    // UNESCAPE it (\n, \", \\, \uXXXX...). The value of a JSON
    // string field is encoded; returning the raw slice hands the
    // caller double-encoded text (the Logogenesis parse bug,
    // 2026-07-31).
    static std::string json_extract_string(const std::string& json,
                                           const std::string& key);
    static std::string json_unescape(const std::string& str);

private:

    // ========================================================================
    // ReAct iteration (copied from LLMSystem for tool calling)
    // ========================================================================
    // NOTE: Message struct is defined in llm_system.h (included above)

    struct ToolCall {
        std::string name;
        std::string args_json;
        bool is_valid() const { return !name.empty(); }
    };

    // Build system prompt with tool definitions for admin mode
    std::string build_admin_system_prompt(const std::string& user_command);

    // Build user prompt with few-shot examples
    std::string build_user_prompt_with_examples(const std::string& user_command);

    // ReAct iteration with message array support (KV cache optimization)
    std::string generate_with_react_iteration(
        const std::string& initial_prompt,
        int max_tokens,
        const std::vector<Message>& input_messages,
        std::vector<Message>& output_messages,
        std::string& output_system_prompt
    );
    bool has_tool_calls(const std::string& response) const;
    ToolCall parse_first_tool_call(const std::string& response) const;
    std::string format_tool_response(const std::string& tool_name,
                                      const std::string& result) const;
};

} // namespace Logosphere
