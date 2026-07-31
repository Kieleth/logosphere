// Logosphere Engine - LLM System
//
// Purpose: Engine-level LLM integration using llama.cpp
// Design: Provides text generation capability to any game built on Logosphere
//
// Architecture notes:
// - Wraps llama.cpp for clean engine integration
// - Loads model once, reuses across game lifetime
// - Greedy sampling for deterministic generation
// - No game-specific logic - just text in, text out
// - Async generation via worker thread (non-blocking)
// - Model-agnostic: works with any GGUF model

#pragma once

#include <string>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <vector>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

// Forward declaration for ToolRegistry
class ToolRegistry;

namespace Logosphere {

// ============================================================================
// LLM TRACING & OBSERVABILITY
// ============================================================================

// LLM trace configuration (runtime configurable for debugging)
struct LLMTraceConfig {
    bool enable_tracing = false;     // Master switch
    bool log_prompts = false;        // Full prompt text (verbose!)
    bool log_tokens = true;          // Token counts
    bool log_timing = true;          // All timing data
    bool log_streaming = false;      // Per-token generation (very verbose!)
    bool log_tool_calls = true;      // Tool call detection
};

// ============================================================================
// ASYNC API - Callback-based generation (non-blocking)
// ============================================================================

// Message structure for conversation history (KV cache optimization)
struct Message {
    std::string role;     // "system", "user", or "assistant"
    std::string content;
};

// Callback signature: (prompt, response, user_data)
// - Invoked on main thread when generation completes
// - prompt: Original prompt submitted
// - response: Generated text
// - user_data: Optional pointer passed to submit_request()
using LLMCallback = std::function<void(const std::string& prompt,
                                        const std::string& response,
                                        void* user_data)>;

// Scene context callback for narrative mode
// Returns current game state as string (player position, nearby entities, etc.)
using SceneContextCallback = std::function<std::string()>;

// Request submitted to worker thread
struct LLMRequest {
    std::string prompt;
    int max_tokens;
    LLMCallback callback;
    void* user_data;
    uint64_t request_id;  // For cancellation (changes per continuation)
    uint64_t root_request_id;  // Root request ID (persists across all ReAct iterations)
    bool is_admin_mode;    // True if root42: command
    std::string original_prompt;  // Original user input before admin prompt wrapping
    int react_iteration = 0;  // Current iteration number (for continuation tracking)
    std::vector<Message> conversation_messages;  // NEW: Message array for KV cache (empty = use prompt field)
};

// Response from worker thread (queued for main thread)
struct LLMResponse {
    std::string prompt;
    std::string response;
    LLMCallback callback;
    void* user_data;
    uint64_t request_id;
    uint64_t root_request_id;  // Root request ID (persists across all ReAct iterations)
    bool cancelled;
    bool error;
    std::string error_message;
    bool is_admin_mode;     // True if root42: command
    std::string original_prompt;  // Original user input

    // ReAct loop state (for main thread orchestration)
    bool has_tool_call = false;
    std::string tool_name;
    std::string tool_args_json;
    std::string react_conversation;  // DEPRECATED: Old string concat (kept for backward compat)
    std::vector<Message> conversation_messages;  // NEW: Message array for KV cache optimization
    std::string system_prompt;       // Fixed system prompt for all iterations
    int react_iteration = 0;         // Current iteration number (0-based)
    int max_react_iterations = 10;   // Max iterations before giving up
};

// ============================================================================
// LLMSystem - Engine-level LLM text generation
// ============================================================================

class LLMSystem {
public:
    LLMSystem();
    ~LLMSystem();

    // ========================================================================
    // INITIALIZATION (blocking, called once at startup)
    // ========================================================================

    // Initialize LLM (load model, create context, start worker thread)
    // model_path: Path to .gguf model file (any model: Qwen, Llama, Mistral, etc.)
    // Returns true on success
    bool initialize(const std::string& model_path);

    // Shutdown LLM (stop worker thread, cleanup resources)
    void shutdown();

    // Check if LLM is ready
    bool is_initialized() const { return initialized_; }

    // Get last error message
    std::string get_last_error() const { return last_error_; }

    // ========================================================================
    // TOOL CALLING (for LLM function calling and admin mode)
    // ========================================================================

    // Register tool registry for function calling
    // Games call this once at startup to provide available tools
    // Pass nullptr to disable tool calling
    void register_tool_registry(ToolRegistry* registry);

    // ========================================================================
    // NARRATIVE MODE (DungeonMaster-style Q&A with scene context)
    // ========================================================================

    // Set system prompt for narrative mode (non-root42 requests)
    // Games load this from file at startup (e.g., "The Weaver" personality)
    void set_narrative_system_prompt(const std::string& prompt);

    // Set callback to get current scene context for narrative mode
    // Called before each narrative request to inject current game state
    // Callback returns scene description string (player position, nearby entities, etc.)
    void set_scene_context_callback(SceneContextCallback callback);

    // ========================================================================
    // ASYNC API (non-blocking, recommended)
    // ========================================================================

    // Submit async request (returns immediately, callback invoked when done)
    // prompt: Input text to complete
    // max_tokens: Maximum tokens to generate
    // callback: Function to call with result (invoked on main thread)
    // user_data: Optional pointer passed to callback (default nullptr)
    // Returns: request_id for cancellation
    //
    // Example use cases:
    // - User chat: "create a cube" → EntityFactory creates cube
    // - NPC dialogue: Generate response, update NPC state
    // - World generation: Generate description, spawn entities
    // - Any future LLM-powered feature
    uint64_t submit_request(const std::string& prompt,
                           int max_tokens,
                           LLMCallback callback,
                           void* user_data = nullptr);

    // Cancel in-flight request (best-effort, may still complete)
    void cancel_request(uint64_t request_id);

    // Process completed responses (called by Engine each frame)
    // Invokes callbacks for all completed requests
    // Returns true if any callbacks were invoked
    bool process_completed_responses();

    // Status queries
    bool has_pending_requests() const;
    int get_queue_size() const;

    // ========================================================================
    // LLM TRACING & OBSERVABILITY
    // ========================================================================

    // Set trace configuration (enable/disable different trace levels)
    void set_trace_config(const LLMTraceConfig& config);

    // ========================================================================
    // SYNC API (blocking, backward compatibility)
    // ========================================================================

    // Generate text synchronously (BLOCKS until complete, 2-3 seconds)
    // Implemented internally using async API
    // Use only for non-interactive scenarios (level loading, etc.)
    std::string generate(const std::string& prompt, int max_tokens = 50);

private:
    // ========================================================================
    // llama.cpp resources (accessed only by worker thread)
    // ========================================================================
    llama_model* model_;
    const llama_vocab* vocab_;
    llama_context* context_;
    llama_sampler* sampler_;
    int max_context_size_;

    // ========================================================================
    // State
    // ========================================================================
    bool initialized_;
    std::string last_error_;

    // ========================================================================
    // Tool calling
    // ========================================================================
    ToolRegistry* tool_registry_;  // Optional, game provides this

    // ========================================================================
    // Narrative mode (DungeonMaster-style Q&A)
    // ========================================================================
    std::string narrative_system_prompt_;  // Loaded from file at startup
    SceneContextCallback scene_context_callback_;  // Returns current scene state

    // ========================================================================
    // Tracing & Observability
    // ========================================================================
    LLMTraceConfig trace_config_;  // Runtime configuration

    // ========================================================================
    // Worker thread
    // ========================================================================
    std::thread worker_thread_;
    std::atomic<bool> shutdown_requested_{false};
    void worker_thread_main();  // Worker thread entry point

    // ========================================================================
    // Thread-safe queues
    // ========================================================================

    // Request queue (main thread → worker thread)
    mutable std::mutex request_mutex_;  // mutable for const method access
    std::queue<LLMRequest> request_queue_;
    std::condition_variable request_cv_;  // Wake worker when request arrives

    // Response queue (worker thread → main thread)
    mutable std::mutex response_mutex_;  // mutable for const method access
    std::queue<LLMResponse> response_queue_;

    // ========================================================================
    // Cancellation
    // ========================================================================
    mutable std::mutex cancel_mutex_;  // mutable for const method access
    std::unordered_set<uint64_t> cancelled_requests_;

    // ========================================================================
    // Request ID generation
    // ========================================================================
    std::atomic<uint64_t> next_request_id_{1};

    // ========================================================================
    // Internal helpers
    // ========================================================================
    void cleanup();  // Cleanup llama.cpp resources
    std::string generate_internal(const std::string& prompt, int max_tokens);  // Actual generation (worker thread only)

    // ========================================================================
    // ReAct iteration helpers (for multi-step tool calling)
    // ========================================================================

    // Tool call structure (parsed from <tool_call> XML)
    struct ToolCall {
        std::string name;
        std::string args_json;  // JSON string for format_tool_response()

        bool is_valid() const { return !name.empty(); }
    };

    // ReAct iteration wrapper for multi-step tool calling
    std::string generate_with_react_iteration(const std::string& initial_prompt, int max_tokens);

    // Check if response contains tool calls
    bool has_tool_calls(const std::string& response) const;

    // Parse first tool call from response (for iterative execution)
    ToolCall parse_first_tool_call(const std::string& response) const;

    // Format tool result as <tool_response> for LLM continuation
    std::string format_tool_response(const std::string& tool_name,
                                      const std::string& result) const;
};

} // namespace Logosphere
