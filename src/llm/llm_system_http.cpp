// Logosphere Engine - LLM System HTTP Implementation
//
// Generic HTTP LLM client supporting multiple providers:
// - MLX (local server, OpenAI-compatible)
// - OpenAI (cloud API)
// - Anthropic (cloud API)

#include "logosphere/llm/llm_system_http.h"
#include "logosphere/llm/http_client.h"
#ifdef LOGOSPHERE_HAS_LLM_HTTPS
#include "logosphere/llm/curl_transport.h"
#endif
#ifdef HAS_LLAMA
#include "tools/tool_registry.h"
#include <nlohmann/json.hpp>
#endif
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>

namespace Logosphere {

// Debug logging
static constexpr bool ENABLE_LLM_DEBUG_LOGS = true;  // ENABLED for HTTP debugging

// ReAct loop configuration
// Increased from 10 to 50 to support complex tasks (e.g., creating 20-40 entities for forest scenes)
// Safety: Loop detection and error tracking will catch problems much earlier
static constexpr int MAX_ITERATIONS = 50;  // Max ReAct iterations before giving up

LLMSystemHTTP::LLMSystemHTTP()
    : initialized_(false)
    , tool_registry_(nullptr)
{
}

LLMSystemHTTP::~LLMSystemHTTP() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool LLMSystemHTTP::initialize(const LLMProviderConfig& config) {
    std::cout << "[LLMSystemHTTP] Initializing with provider: ";
    switch (config.provider) {
        case LLMProvider::MLX:      std::cout << "MLX"; break;
        case LLMProvider::OpenAI:    std::cout << "OpenAI"; break;
        case LLMProvider::Anthropic: std::cout << "Anthropic"; break;
        case LLMProvider::Custom:    std::cout << "Custom"; break;
    }
    std::cout << std::endl;
    std::cout << "[LLMSystemHTTP] Endpoint: " << config.endpoint_url << std::endl;
    std::cout << "[LLMSystemHTTP] Model: " << config.model_name << std::endl;

    config_ = config;

    // Parse URL: scheme + host + port. Three forms accepted:
    //   "https://api.anthropic.com"
    //   "http://localhost:8000"
    //   "localhost:8000"  (legacy, scheme inferred from provider)
    std::string host;
    int port;
    bool use_https = false;

    std::string url = config.endpoint_url;
    if (url.find("https://") == 0) {
        use_https = true;
        url = url.substr(8);
    } else if (url.find("http://") == 0) {
        use_https = false;
        url = url.substr(7);
    } else {
        // No scheme; default per provider — cloud APIs are HTTPS,
        // localhost MLX is plain HTTP.
        use_https = (config.provider == LLMProvider::OpenAI ||
                     config.provider == LLMProvider::Anthropic);
    }

    // Extract host (strip path) and optional port.
    std::string host_and_port = url;
    size_t slash_pos = host_and_port.find('/');
    if (slash_pos != std::string::npos) {
        host_and_port = host_and_port.substr(0, slash_pos);
    }
    size_t colon_pos = host_and_port.find(':');
    if (colon_pos != std::string::npos) {
        host = host_and_port.substr(0, colon_pos);
        port = std::stoi(host_and_port.substr(colon_pos + 1));
    } else {
        host = host_and_port;
        port = use_https ? 443 : (config.provider == LLMProvider::MLX ? 8000 : 80);
    }

    std::cout << "[LLMSystemHTTP] Connecting to "
              << (use_https ? "https://" : "http://")
              << host << ":" << port << std::endl;

    // Pick transport. HTTPS → CurlTransport (libcurl, TLS, SNI).
    // Plain HTTP → HTTPClient (POSIX TCP). Build-time guard:
    // CurlTransport only exists when LOGOSPHERE_LLM_HTTPS=ON.
    if (use_https) {
#ifdef LOGOSPHERE_HAS_LLM_HTTPS
        http_client_ = std::make_unique<CurlTransport>(host, port, true);
#else
        last_error_ = "HTTPS LLM endpoint requested (" + host
                    + ") but logosphere was built without "
                    + "LOGOSPHERE_LLM_HTTPS. Rebuild with libcurl "
                    + "available (find_package(CURL)) to enable "
                    + "cloud LLM APIs.";
        std::cerr << "[LLMSystemHTTP] ERROR: " << last_error_ << std::endl;
        return false;
#endif
    } else {
        http_client_ = std::make_unique<HTTPClient>(host, port);
    }
    rebuild_auth_headers_();

    // Test connection
    if (!http_client_->is_reachable()) {
        last_error_ = "Failed to connect to LLM server at " + host + ":" + std::to_string(port);
        std::cerr << "[LLMSystemHTTP] ERROR: " << last_error_ << std::endl;
        std::cerr << "[LLMSystemHTTP] Make sure the server is running!" << std::endl;
        return false;
    }

    std::cout << "[LLMSystemHTTP] Connected successfully" << std::endl;

    // Start worker thread
    shutdown_requested_ = false;
    worker_thread_ = std::thread(&LLMSystemHTTP::worker_thread_main, this);

    initialized_ = true;
    return true;
}

bool LLMSystemHTTP::initialize_mlx(const std::string& url, const std::string& model) {
    LLMProviderConfig config;
    config.provider = LLMProvider::MLX;
    config.endpoint_url = url;
    config.model_name = model;
    config.completion_path = "/v1/chat/completions";  // Use chat endpoint for instruct models
    return initialize(config);
}

bool LLMSystemHTTP::initialize_openai(const std::string& api_key, const std::string& model) {
    LLMProviderConfig config;
    config.provider = LLMProvider::OpenAI;
    config.endpoint_url = "https://api.openai.com";
    config.api_key = api_key;
    config.model_name = model;
    config.completion_path = "/v1/chat/completions";
    return initialize(config);
}

bool LLMSystemHTTP::initialize_anthropic(const std::string& api_key, const std::string& model) {
    LLMProviderConfig config;
    config.provider = LLMProvider::Anthropic;
    config.endpoint_url = "https://api.anthropic.com";
    config.api_key = api_key;
    config.model_name = model;
    config.completion_path = "/v1/messages";
    return initialize(config);
}

void LLMSystemHTTP::shutdown() {
    if (!initialized_) return;

    std::cout << "[LLMSystemHTTP] Shutting down..." << std::endl;

    // Signal worker thread to stop
    shutdown_requested_ = true;
    request_cv_.notify_all();

    // Wait for worker thread
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    http_client_.reset();
    initialized_ = false;

    std::cout << "[LLMSystemHTTP] Shutdown complete" << std::endl;
}

// ============================================================================
// Tool Registry
// ============================================================================

void LLMSystemHTTP::register_tool_registry(ToolRegistry* registry) {
    tool_registry_ = registry;
    if (registry) {
        std::cout << "[LLMSystemHTTP] Tool registry registered" << std::endl;
    }
}

void LLMSystemHTTP::set_trace_config(const LLMTraceConfig& config) {
    trace_config_ = config;
}

// Set narrative system prompt (DungeonMaster personality)
void LLMSystemHTTP::set_narrative_system_prompt(const std::string& prompt) {
    narrative_system_prompt_ = prompt;
    std::cout << "[LLMSystemHTTP] Narrative system prompt set (" << prompt.length() << " chars)" << std::endl;
}

// Set scene context callback (provides current game state)
void LLMSystemHTTP::set_scene_context_callback(SceneContextCallback callback) {
    scene_context_callback_ = callback;
    std::cout << "[LLMSystemHTTP] Scene context callback registered" << std::endl;
}

void LLMSystemHTTP::warmup_narrative() {
    if (!initialized_ || narrative_system_prompt_.empty()) {
        std::cout << "[LLMSystemHTTP] Warmup skipped (not initialized or no narrative prompt)" << std::endl;
        return;
    }

    std::cout << "[LLMSystemHTTP] Warming up narrative system prompt..." << std::endl;

    // Fire-and-forget: send a minimal request to prime the KV cache
    // The response is ignored (callback does nothing)
    submit_request(
        "Hello",  // Minimal user message
        5,        // Very short response (just need to process system prompt)
        [](const std::string&, const std::string&, void*) {
            std::cout << "[LLMSystemHTTP] Warmup complete" << std::endl;
        },
        nullptr
    );
}

// ============================================================================
// Async API
// ============================================================================

uint64_t LLMSystemHTTP::submit_request(const std::string& prompt,
                                        int max_tokens,
                                        LLMCallback callback,
                                        void* user_data) {
    if (!initialized_) {
        std::cerr << "[LLMSystemHTTP] ERROR: Not initialized!" << std::endl;
        return 0;
    }

    uint64_t request_id = next_request_id_++;

    LLMRequest request;
    request.prompt = prompt;
    request.max_tokens = max_tokens;
    request.callback = callback;
    request.user_data = user_data;
    request.request_id = request_id;
    request.root_request_id = request_id;  // Initial request: root_id == request_id
    request.is_admin_mode = (prompt.find("root42:") == 0);
    request.original_prompt = prompt;

    // Queue request
    {
        std::unique_lock<std::mutex> lock(request_mutex_);
        request_queue_.push(request);
    }

    // Wake worker thread
    request_cv_.notify_one();

    return request_id;
}

void LLMSystemHTTP::cancel_request(uint64_t request_id) {
    std::unique_lock<std::mutex> lock(cancel_mutex_);
    cancelled_requests_.insert(request_id);
}

bool LLMSystemHTTP::process_completed_responses() {
    bool processed_any = false;

    while (true) {
        LLMResponse response;

        // Pop response from queue
        {
            std::unique_lock<std::mutex> lock(response_mutex_);
            if (response_queue_.empty()) break;

            response = response_queue_.front();
            response_queue_.pop();
        }

        // Check if cancelled
        {
            std::unique_lock<std::mutex> lock(cancel_mutex_);
            if (cancelled_requests_.count(response.request_id)) {
                cancelled_requests_.erase(response.request_id);
                continue;  // Skip cancelled request
            }
        }

        // ====================================================================
        // ReAct Orchestration (Main Thread)
        // ====================================================================
        // Tools are NOW safe to call here (non-blocking - just create KG entities)
        // If response has tool call, execute it and submit continuation request

        if (response.has_tool_call &&
            response.react_iteration < response.max_react_iterations &&
            tool_registry_) {

            std::cout << "\n[MainThread] ReAct iteration " << (response.react_iteration + 1)
                      << "/" << response.max_react_iterations << ": Executing tool '"
                      << response.tool_name << "'" << std::endl;

            // Execute tool on main thread (NOW non-blocking!)
            std::string tool_result;
#ifdef HAS_LLAMA
            try {
                auto args_json = nlohmann::json::parse(response.tool_args_json);
                auto tool_start = std::chrono::high_resolution_clock::now();
                tool_result = tool_registry_->execute_tool(response.tool_name, args_json);
                auto tool_end = std::chrono::high_resolution_clock::now();
                auto tool_ms = std::chrono::duration<double, std::milli>(tool_end - tool_start).count();
                if (tool_ms > 1.0) {
                    std::cout << "[STALL-TOOL] " << response.tool_name << " took " << tool_ms << "ms" << std::endl;
                }
                std::cout << "[MainThread] Tool executed: " << tool_result.substr(0, 100)
                          << (tool_result.length() > 100 ? "..." : "") << std::endl;
            } catch (const std::exception& e) {
                tool_result = "[ERROR: " + std::string(e.what()) + "]";
                std::cerr << "[MainThread] Tool execution failed: " << e.what() << std::endl;
            }
#else
            tool_result = "[ERROR: Tool execution requires HAS_LLAMA]";
            std::cerr << "[MainThread] Tool execution disabled - rebuild with llama.cpp support" << std::endl;
#endif

            // ====================================================================
            // Loop Detection (ADK pattern: 3+ repetitions in 5-call window)
            // Option D: Track both input AND output to handle randomized tools
            // ====================================================================
            struct ToolCallHistory {
                std::string tool_name;
                size_t args_hash;      // Hash of input arguments
                size_t result_hash;    // Hash of output result (NEW: catches randomization)
            };
            static std::unordered_map<uint64_t, std::vector<ToolCallHistory>> call_history;
            const int REPETITION_WINDOW = 5;
            const int REPETITION_THRESHOLD = 2;  // Trigger on 3rd occurrence (2 previous + 1 current)

            size_t args_hash = std::hash<std::string>{}(response.tool_args_json);
            size_t result_hash = std::hash<std::string>{}(tool_result);  // NEW: Hash tool output
            ToolCallHistory current = {response.tool_name, args_hash, result_hash};
            std::cout << "[LoopDetect] Iteration " << (response.react_iteration + 1)
                      << " - Tool: " << response.tool_name
                      << ", ArgsHash: " << args_hash
                      << ", ResultHash: " << result_hash
                      << ", RootReqID: " << response.root_request_id << std::endl;

            auto& history = call_history[response.root_request_id];  // Use root_request_id for persistence
            std::cout << "[LoopDetect] History size: " << history.size() << " items" << std::endl;

            // Count repetitions in recent window (BOTH args AND result must match)
            int repeat_count = 0;
            for (size_t i = 0; i < history.size(); i++) {
                const auto& prev = history[i];
                bool match = (prev.tool_name == current.tool_name &&
                             prev.args_hash == current.args_hash &&
                             prev.result_hash == current.result_hash);  // NEW: Result must also match
                std::cout << "[LoopDetect]   History[" << i << "]: "
                          << prev.tool_name << ", args=" << prev.args_hash
                          << ", result=" << prev.result_hash
                          << " -> " << (match ? "MATCH" : "different") << std::endl;
                if (match) {
                    repeat_count++;
                }
            }

            std::cout << "[LoopDetect] Final repeat_count: " << repeat_count
                      << " (threshold: " << REPETITION_THRESHOLD << ")" << std::endl;

            if (repeat_count >= REPETITION_THRESHOLD) {
                std::cout << "[ReAct] Loop detected: " << response.tool_name
                          << " repeated " << (repeat_count + 1) << " times with identical args AND results"
                          << " (stopping at iteration " << (response.react_iteration + 1) << ")"
                          << std::endl;

                // Invoke final callback (stop loop)
                if (response.callback) {
                    response.callback(response.prompt,
                                     response.response + "\n\n[Stopped: repetitive behavior detected - same tool call producing same result]",
                                     response.user_data);
                }

                call_history.erase(response.root_request_id);  // Cleanup
                return true;  // Handled
            }

            history.push_back(current);
            if (history.size() > REPETITION_WINDOW) {
                history.erase(history.begin());
            }

            // ====================================================================
            // Error Accumulation (LangChain pattern: stop after 3 consecutive failures)
            // ====================================================================
            static std::unordered_map<uint64_t, int> error_counts;
            const int MAX_CONSECUTIVE_ERRORS = 3;

            // Detect if tool execution failed (check for "error" in JSON result)
            bool tool_failed = (tool_result.find("\"error\"") != std::string::npos);

            if (tool_failed) {
                error_counts[response.root_request_id]++;
                std::cout << "[ErrorTrack] Tool failed. Error count: "
                          << error_counts[response.root_request_id]
                          << " / " << MAX_CONSECUTIVE_ERRORS
                          << " (RootReqID: " << response.root_request_id << ")"
                          << std::endl;

                if (error_counts[response.root_request_id] >= MAX_CONSECUTIVE_ERRORS) {
                    std::cout << "[ReAct] Too many consecutive errors ("
                              << MAX_CONSECUTIVE_ERRORS << ") - stopping at iteration "
                              << (response.react_iteration + 1) << std::endl;

                    // Invoke final callback (stop loop)
                    if (response.callback) {
                        response.callback(response.prompt,
                                         response.response + "\n\n[Stopped: "
                                         + std::to_string(MAX_CONSECUTIVE_ERRORS)
                                         + " consecutive tool failures]",
                                         response.user_data);
                    }

                    error_counts.erase(response.root_request_id);  // Cleanup
                    call_history.erase(response.root_request_id);  // Cleanup loop detection too
                    return true;  // Handled
                }
            } else {
                // Success - reset error counter
                if (error_counts[response.root_request_id] > 0) {
                    std::cout << "[ErrorTrack] Tool succeeded. Resetting error count (was "
                              << error_counts[response.root_request_id] << ")"
                              << std::endl;
                    error_counts[response.root_request_id] = 0;
                }
            }

            // Build continuation message array: previous messages + assistant response + tool result
            std::vector<Message> continuation_messages = response.conversation_messages;

            // Append assistant's response with tool call
            continuation_messages.push_back({
                .role = "assistant",
                .content = response.response
            });

            // Append tool result as user message
            std::string formatted_tool_result = format_tool_response(response.tool_name, tool_result);
            continuation_messages.push_back({
                .role = "user",
                .content = formatted_tool_result
            });

            std::cout << "[MainThread] Submitting continuation request (iteration "
                      << (response.react_iteration + 2) << ") with "
                      << continuation_messages.size() << " messages" << std::endl;

            // Submit continuation request with updated ReAct state
            LLMRequest continuation_request;
            // CRITICAL: Pass message array (enables MLX KV cache), NOT string concatenation!
            continuation_request.prompt = "";  // Empty for message array mode
            continuation_request.conversation_messages = continuation_messages;
            continuation_request.max_tokens = 1000;
            continuation_request.callback = response.callback;
            continuation_request.user_data = response.user_data;
            continuation_request.request_id = next_request_id_++;  // New request_id for cancellation
            continuation_request.root_request_id = response.root_request_id;  // Preserve root ID for loop detection
            continuation_request.is_admin_mode = true;
            continuation_request.original_prompt = response.original_prompt;
            continuation_request.react_iteration = response.react_iteration + 1;  // Increment iteration counter

            {
                std::unique_lock<std::mutex> lock(request_mutex_);
                request_queue_.push(continuation_request);
            }
            request_cv_.notify_one();

            // Don't invoke callback yet - wait for final response

        } else {
            // No tool call OR max iterations reached - invoke final callback
            if (response.react_iteration >= response.max_react_iterations) {
                std::cout << "[MainThread] Max ReAct iterations (" << response.max_react_iterations
                          << ") reached. Invoking final callback." << std::endl;
            }

            if (response.callback) {
                response.callback(response.prompt, response.response, response.user_data);
            }

            // Cleanup loop detection history and error counts (defined above in tool call block)
            struct ToolCallHistory {
                std::string tool_name;
                size_t args_hash;
            };
            static std::unordered_map<uint64_t, std::vector<ToolCallHistory>> call_history;
            static std::unordered_map<uint64_t, int> error_counts;

            call_history.erase(response.root_request_id);
            error_counts.erase(response.root_request_id);
        }

        processed_any = true;
    }

    return processed_any;
}

bool LLMSystemHTTP::has_pending_requests() const {
    std::unique_lock<std::mutex> lock(request_mutex_);
    return !request_queue_.empty();
}

int LLMSystemHTTP::get_queue_size() const {
    std::unique_lock<std::mutex> lock(request_mutex_);
    return request_queue_.size();
}

// ============================================================================
// Sync API
// ============================================================================

std::string LLMSystemHTTP::generate(const std::string& prompt, int max_tokens) {
    if (!initialized_) {
        return "[ERROR: LLM not initialized]";
    }

    // Synchronous API now uses async infrastructure internally to benefit from
    // main thread ReAct orchestration (non-blocking tool execution)

    // Result storage for callback
    std::string final_response;
    bool response_received = false;

    // Submit request using async API
    submit_request(prompt, max_tokens, [&](const std::string& prompt, const std::string& response, void* user_data) {
        final_response = response;
        response_received = true;
    }, nullptr);

    // Poll process_completed_responses() until we get the final result
    // This processes ReAct iterations on "main thread" (the calling thread)
    while (!response_received) {
        process_completed_responses();
        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return final_response;
}

// ============================================================================
// Worker Thread
// ============================================================================

void LLMSystemHTTP::worker_thread_main() {
    while (!shutdown_requested_) {
        LLMRequest request;

        // Wait for request
        {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return !request_queue_.empty() || shutdown_requested_;
            });

            if (shutdown_requested_) break;

            request = request_queue_.front();
            request_queue_.pop();
        }

        // Check if cancelled
        {
            std::unique_lock<std::mutex> lock(cancel_mutex_);
            if (cancelled_requests_.count(request.request_id)) {
                cancelled_requests_.erase(request.request_id);
                continue;
            }
        }

        // Generate response
        std::string response_text;
        bool error = false;
        std::string error_msg;

        // Build conversation_messages for response (NEW: KV cache optimization)
        std::vector<Message> conversation_messages_for_response;
        std::string system_prompt_for_response;

        try {
            if (tool_registry_ && request.is_admin_mode) {
                // Use ReAct iteration for tool calling (with message arrays)
                response_text = generate_with_react_iteration(
                    request.prompt,
                    request.max_tokens,
                    request.conversation_messages,
                    conversation_messages_for_response,
                    system_prompt_for_response
                );
            } else if (!narrative_system_prompt_.empty()) {
                // NARRATIVE MODE: Build DungeonMaster-style prompt with scene context
                std::string system_prompt = narrative_system_prompt_;

                // Inject scene context if callback is set
                if (scene_context_callback_) {
                    system_prompt += "\n\n";
                    auto ctx_start = std::chrono::high_resolution_clock::now();
                    system_prompt += scene_context_callback_();
                    auto ctx_end = std::chrono::high_resolution_clock::now();
                    auto ctx_ms = std::chrono::duration<double, std::milli>(ctx_end - ctx_start).count();
                    if (ctx_ms > 5.0) {
                        std::cout << "[STALL-CONTEXT] Scene context took " << ctx_ms << "ms" << std::endl;
                    }
                }

                // Build provider-specific request with system + user messages
                std::string request_json;
                switch (config_.provider) {
                    case LLMProvider::MLX:
                        request_json = build_mlx_request_with_system(system_prompt, request.prompt, request.max_tokens);
                        break;
                    case LLMProvider::OpenAI:
                        request_json = build_openai_request_with_system(system_prompt, request.prompt, request.max_tokens);
                        break;
                    case LLMProvider::Anthropic:
                        request_json = build_anthropic_request_with_system(system_prompt, request.prompt, request.max_tokens);
                        break;
                    default:
                        request_json = build_mlx_request_with_system(system_prompt, request.prompt, request.max_tokens);
                        break;
                }

                std::cout << "[NARRATIVE_DEBUG] Built narrative prompt (system=" << system_prompt.length()
                          << " chars, user=" << request.prompt.length() << " chars)" << std::endl;

                // Make HTTP request and parse response
                HTTPResponse http_response = http_client_->post(
                    config_.completion_path,
                    request_json,
                    "application/json",
                    auth_headers_
                );

                if (http_response.error || http_response.status_code != 200) {
                    throw std::runtime_error("HTTP error: " + http_response.error_message);
                }

                response_text = parse_response_json(http_response.body);
                if (response_text.empty()) {
                    std::cerr << "[LLMSystemHTTP] ERROR: 200 response but "
                                 "extraction found no text; body head: "
                              << http_response.body.substr(0, 300)
                              << std::endl;
                    throw std::runtime_error(
                        "empty extraction from 200 response");
                }
            } else {
                // Simple generation (no system prompt)
                response_text = generate_internal(request.prompt, request.max_tokens);
            }
        } catch (const std::exception& e) {
            error = true;
            error_msg = e.what();
            response_text = "[ERROR: " + error_msg + "]";
        }

        // Parse tool calls from response (for ReAct orchestration on main thread)
        bool has_tool_call = false;
        std::string tool_name;
        std::string tool_args_json;
        std::string react_conversation;  // DEPRECATED but kept for backward compat

        // Enable tool calling for admin mode OR narrative mode (Weaver DM can use tools)
        if (tool_registry_ && (request.is_admin_mode || !narrative_system_prompt_.empty()) && !error) {
            has_tool_call = has_tool_calls(response_text);
            if (has_tool_call) {
                ToolCall parsed_tool = parse_first_tool_call(response_text);
                tool_name = parsed_tool.name;
                tool_args_json = parsed_tool.args_json;

                if (ENABLE_LLM_DEBUG_LOGS) {
                    std::cout << "[Worker] Parsed tool call: " << tool_name << std::endl;
                    std::cout << "[Worker] Tool args: " << tool_args_json << std::endl;
                }
            }

            // Build deprecated react_conversation for backward compat
            if (request.prompt.find("root42:") == 0) {
                react_conversation = response_text;
            } else {
                react_conversation = request.prompt + response_text;
            }
        }

        // Queue response
        {
            std::unique_lock<std::mutex> lock(response_mutex_);
            response_queue_.push(LLMResponse{
                .prompt = request.prompt,
                .response = response_text,
                .callback = request.callback,
                .user_data = request.user_data,
                .request_id = request.request_id,
                .root_request_id = request.root_request_id,  // Persist across all ReAct iterations
                .cancelled = false,
                .error = error,
                .error_message = error_msg,
                .is_admin_mode = request.is_admin_mode,
                .original_prompt = request.original_prompt,

                // ReAct loop state (for main thread orchestration)
                .has_tool_call = has_tool_call,
                .tool_name = tool_name,
                .tool_args_json = tool_args_json,
                .react_conversation = react_conversation,  // DEPRECATED
                .conversation_messages = conversation_messages_for_response,  // NEW: KV cache optimization
                .system_prompt = system_prompt_for_response,  // NEW: System prompt for continuations
                .react_iteration = request.react_iteration,  // Copy from request (0 for first, increments for continuations)
                .max_react_iterations = MAX_ITERATIONS
            });
        }
    }
}

// ============================================================================
// Internal Generation
// ============================================================================

std::string LLMSystemHTTP::generate_internal(const std::string& prompt, int max_tokens) {
    auto start_time = std::chrono::steady_clock::now();

    // Build request JSON
    std::string request_json = build_request_json(prompt, max_tokens);

    if (ENABLE_LLM_DEBUG_LOGS) {
        std::cout << "[LLMSystemHTTP] Request JSON: " << request_json << std::endl;
    }

    // Make HTTP POST request
    HTTPResponse http_response = http_client_->post(
        config_.completion_path,
        request_json,
        "application/json",
        auth_headers_
    );

    if (http_response.error) {
        throw std::runtime_error("HTTP request failed: " + http_response.error_message);
    }

    if (http_response.status_code != 200) {
        throw std::runtime_error("HTTP error " + std::to_string(http_response.status_code));
    }

    if (ENABLE_LLM_DEBUG_LOGS) {
        std::cout << "[LLMSystemHTTP] Response JSON: " << http_response.body << std::endl;
    }

    // Parse response JSON
    std::string response_text = parse_response_json(http_response.body);

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    if (trace_config_.enable_tracing && trace_config_.log_timing) {
        std::cout << "[LLMSystemHTTP] Generation took " << duration_ms << "ms" << std::endl;
    }

    return response_text;
}

// ============================================================================
// Provider-Specific JSON Builders
// ============================================================================

std::string LLMSystemHTTP::build_request_json(const std::string& prompt, int max_tokens) {
    switch (config_.provider) {
        case LLMProvider::MLX:
            return build_mlx_request(prompt, max_tokens);
        case LLMProvider::OpenAI:
            return build_openai_request(prompt, max_tokens);
        case LLMProvider::Anthropic:
            return build_anthropic_request(prompt, max_tokens);
        default:
            return build_mlx_request(prompt, max_tokens);  // Default to MLX format
    }
}

std::string LLMSystemHTTP::build_mlx_request(const std::string& prompt, int max_tokens) {
    // Use chat format for instruct models (same as OpenAI)
    // NOTE: Sampling parameters (temperature, top_k, etc.) configured at server startup
    // See: docs/LLM_SAMPLING_PARAMETERS.md for rationale
    // Client can override by adding params here if needed per-request
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"messages\":[";
    json << "{\"role\":\"user\",\"content\":\"" << json_escape(prompt) << "\"}";
    json << "],";
    json << "\"max_tokens\":" << max_tokens;
    // No sampling params - use server defaults (temp 0.6, top_k 20, top_p 0.95, repeat_penalty 1.1)
    json << "}";
    return json.str();
}

std::string LLMSystemHTTP::build_openai_request(const std::string& prompt, int max_tokens) {
    // OpenAI API - use their defaults (temp typically 1.0)
    // Override per-request if needed by adding parameters
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"messages\":[";
    json << "{\"role\":\"user\",\"content\":\"" << json_escape(prompt) << "\"}";
    json << "],";
    json << "\"max_tokens\":" << max_tokens;
    // Use API defaults for sampling
    json << "}";
    return json.str();
}

std::string LLMSystemHTTP::build_anthropic_request(const std::string& prompt, int max_tokens) {
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"max_tokens\":" << max_tokens << ",";
    json << "\"messages\":[";
    json << "{\"role\":\"user\",\"content\":\"" << json_escape(prompt) << "\"}";
    json << "]";
    json << "}";
    return json.str();
}

// ============================================================================
// Multi-Message Builders (system + user for tool calling)
// ============================================================================

std::string LLMSystemHTTP::build_mlx_request_with_system(const std::string& system_prompt,
                                                            const std::string& user_prompt,
                                                            int max_tokens) {
    // OpenAI-compatible format with system message
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"messages\":[";
    json << "{\"role\":\"system\",\"content\":\"" << json_escape(system_prompt) << "\"},";
    json << "{\"role\":\"user\",\"content\":\"" << json_escape(user_prompt) << "\"}";
    json << "],";
    json << "\"max_tokens\":" << max_tokens << ",";
    // Stop sequences: prevent model from continuing conversation as multiple turns
    // - "</tool_call>" for admin mode
    // - "You:" / "\nYou:" to stop when model tries to generate user's next turn
    // - "The Weaver:" to stop if model tries to generate another response
    json << "\"stop\":[\"</tool_call>\", \"You:\", \"\\nYou:\", \"The Weaver:\", \"\\nThe Weaver:\"]";
    json << "}";
    return json.str();
}

std::string LLMSystemHTTP::build_openai_request_with_system(const std::string& system_prompt,
                                                              const std::string& user_prompt,
                                                              int max_tokens) {
    // Same format as MLX (OpenAI-compatible)
    return build_mlx_request_with_system(system_prompt, user_prompt, max_tokens);
}

std::string LLMSystemHTTP::build_anthropic_request_with_system(const std::string& system_prompt,
                                                                 const std::string& user_prompt,
                                                                 int max_tokens) {
    // Anthropic uses system parameter at root level, not in messages
    // array. We emit the system block as the structured array form
    // tagged with cache_control: ephemeral so the server-side prompt
    // cache kicks in. For Sonnet/Opus 4.x the system block must be
    // ≥1024 tokens for the cache to actually populate (smaller blocks
    // are silently uncached); the API still accepts cache_control on
    // shorter blocks, just no-ops it. Caller's responsibility to make
    // the system content big enough — embedding the ontology in the
    // Director's system prompt is what gets us over the line.
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"system\":[{\"type\":\"text\",\"text\":\""
         << json_escape(system_prompt)
         << "\",\"cache_control\":{\"type\":\"ephemeral\"}}],";
    json << "\"max_tokens\":" << max_tokens << ",";
    json << "\"messages\":[";
    json << "{\"role\":\"user\",\"content\":\"" << json_escape(user_prompt) << "\"}";
    json << "]";
    json << "}";
    return json.str();
}

// ============================================================================
// Message Array Builders (for KV cache optimization)
// ============================================================================

std::string LLMSystemHTTP::build_mlx_request_with_messages(const std::vector<Message>& messages, int max_tokens) {
    // OpenAI-compatible format with message array
    // Enables MLX KV cache optimization via system prompt caching
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";
    json << "\"messages\":[";

    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"role\":\"" << json_escape(messages[i].role) << "\",";
        json << "\"content\":\"" << json_escape(messages[i].content) << "\"}";
    }

    json << "],";
    json << "\"max_tokens\":" << max_tokens << ",";
    json << "\"stop\":[\"</tool_call>\"]";
    json << "}";
    return json.str();
}

std::string LLMSystemHTTP::build_openai_request_with_messages(const std::vector<Message>& messages, int max_tokens) {
    // Same format as MLX (OpenAI-compatible)
    return build_mlx_request_with_messages(messages, max_tokens);
}

std::string LLMSystemHTTP::build_anthropic_request_with_messages(const std::vector<Message>& messages, int max_tokens) {
    // Anthropic: system message extracted to root level, rest in messages array
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"" << json_escape(config_.model_name) << "\",";

    // Extract system message (first message with role="system")
    std::string system_prompt;
    std::vector<Message> non_system_messages;
    for (const auto& msg : messages) {
        if (msg.role == "system" && system_prompt.empty()) {
            system_prompt = msg.content;
        } else {
            non_system_messages.push_back(msg);
        }
    }

    if (!system_prompt.empty()) {
        json << "\"system\":\"" << json_escape(system_prompt) << "\",";
    }

    json << "\"max_tokens\":" << max_tokens << ",";
    json << "\"messages\":[";

    for (size_t i = 0; i < non_system_messages.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"role\":\"" << json_escape(non_system_messages[i].role) << "\",";
        json << "\"content\":\"" << json_escape(non_system_messages[i].content) << "\"}";
    }

    json << "]";
    json << "}";
    return json.str();
}

// ============================================================================
// Provider-Specific JSON Parsers
// ============================================================================

std::string LLMSystemHTTP::parse_response_json(const std::string& json_response) {
    switch (config_.provider) {
        case LLMProvider::MLX:
            return parse_mlx_response(json_response);
        case LLMProvider::OpenAI:
            return parse_openai_response(json_response);
        case LLMProvider::Anthropic:
            return parse_anthropic_response(json_response);
        default:
            return parse_mlx_response(json_response);
    }
}

std::string LLMSystemHTTP::parse_mlx_response(const std::string& json) {
    // MLX native OpenAI-compatible format:
    // {"choices":[{"message":{"content":"", "tool_calls":[{"function":{"name":"...","arguments":"..."}}]}}]}

    std::cout << "[LLMSystemHTTP] Parsing MLX response (" << json.length() << " bytes)" << std::endl;

    // Navigate to message
    size_t message_pos = json.find("\"message\":");
    if (message_pos == std::string::npos) {
        std::cerr << "[LLMSystemHTTP] ERROR: 'message' not found" << std::endl;
        return "";
    }

    // Check for tool_calls (MLX native tool calling)
    size_t tool_calls_pos = json.find("\"tool_calls\":", message_pos);
    if (tool_calls_pos != std::string::npos) {
        // Check if tool_calls array is empty []
        size_t bracket_start = json.find("[", tool_calls_pos);
        if (bracket_start != std::string::npos) {
            size_t bracket_end = json.find_first_not_of(" \t\n", bracket_start + 1);
            if (bracket_end != std::string::npos && json[bracket_end] == ']') {
                std::cout << "[LLMSystemHTTP] tool_calls array is empty, checking content field" << std::endl;
                // Fall through to content parsing below
            } else {
                // Non-empty tool_calls array - parse it
                size_t name_pos = json.find("\"name\":", tool_calls_pos);
                if (name_pos == std::string::npos) {
                    std::cerr << "[LLMSystemHTTP] ERROR: 'name' not found in tool_calls" << std::endl;
                    return "";
                }

                size_t name_start = json.find("\"", name_pos + 7) + 1;
                size_t name_end = json.find("\"", name_start);
                std::string func_name = json.substr(name_start, name_end - name_start);

                // Extract function arguments (JSON string)
                size_t args_pos = json.find("\"arguments\":", name_pos);
                if (args_pos == std::string::npos) {
                    std::cerr << "[LLMSystemHTTP] ERROR: 'arguments' not found in tool_calls" << std::endl;
                    return "";
                }

                size_t args_start = json.find("\"", args_pos + 12) + 1;
                size_t args_end = args_start;

                // Find closing quote (handle escaped quotes)
                while (args_end < json.length()) {
                    if (json[args_end] == '"' && (args_end == 0 || json[args_end-1] != '\\')) {
                        break;
                    }
                    args_end++;
                }
                std::string args_json = json.substr(args_start, args_end - args_start);

                // Unescape JSON string from MLX (MLX returns arguments as escaped JSON string)
                // Replace backslash-quote with quote and backslash-backslash with backslash
                std::string unescaped_args;
                for (size_t i = 0; i < args_json.length(); i++) {
                    if (args_json[i] == '\\' && i + 1 < args_json.length()) {
                        char next = args_json[i + 1];
                        if (next == '"' || next == '\\') {
                            unescaped_args += next;
                            i++;  // Skip the backslash
                            continue;
                        }
                    }
                    unescaped_args += args_json[i];
                }

                // Convert to Hermes XML format for ReAct loop
                std::ostringstream result;
                result << "<tool_call>\n";
                result << "{\"name\": \"" << func_name << "\", \"arguments\": " << unescaped_args << "}\n";
                result << "</tool_call>";

                std::cout << "[LLMSystemHTTP] Tool call: " << func_name << std::endl;
                return result.str();
            }
        }
    }

    // No tool_calls - check content field (model might respond with text)
    size_t content_pos = json.find("\"content\":", message_pos);
    if (content_pos == std::string::npos) {
        std::cerr << "[LLMSystemHTTP] ERROR: Neither 'tool_calls' nor 'content' found" << std::endl;
        return "";
    }

    size_t content_start = json.find("\"", content_pos + 10) + 1;
    size_t content_end = content_start;

    while (content_end < json.length()) {
        if (json[content_end] == '"' && (content_end == 0 || json[content_end-1] != '\\')) {
            break;
        }
        content_end++;
    }

    std::string content = json.substr(content_start, content_end - content_start);

    // Decode the JSON-encoded value (quotes, newlines, \uXXXX...).
    // Was a partial hand-roll; unified on the tested helper when the
    // Anthropic path gained the same decoding (2026-07-31).
    content = json_unescape(content);

    // Check if content is a JSON tool call: {"name": "...", "arguments": {...}}
    if (content.find("{\"name\":") != std::string::npos) {
        std::cout << "[LLMSystemHTTP] Content contains JSON tool call, parsing..." << std::endl;

        // Parse tool name and arguments
        size_t name_pos = content.find("\"name\":");
        size_t args_pos = content.find("\"arguments\":");

        if (name_pos != std::string::npos && args_pos != std::string::npos) {
            // Extract name
            size_t name_start = content.find("\"", name_pos + 7) + 1;
            size_t name_end = content.find("\"", name_start);
            std::string func_name = content.substr(name_start, name_end - name_start);

            // Extract arguments (from opening { to closing })
            size_t args_start = content.find("{", args_pos);
            if (args_start != std::string::npos) {
                int brace_count = 1;
                size_t args_end = args_start + 1;
                while (args_end < content.length() && brace_count > 0) {
                    if (content[args_end] == '{') brace_count++;
                    else if (content[args_end] == '}') brace_count--;
                    args_end++;
                }
                std::string args_json = content.substr(args_start, args_end - args_start);

                // Convert to Hermes XML format
                std::ostringstream result;
                result << "<tool_call>\n";
                result << "{\"name\": \"" << func_name << "\", \"arguments\": " << args_json << "}\n";
                result << "</tool_call>";

                std::cout << "[LLMSystemHTTP] Tool call from content: " << func_name << std::endl;
                return result.str();
            }
        }
    }

    // Debug: show full content if it might contain tool call tags
    if (content.find("<tool") != std::string::npos || content.find("tool_call") != std::string::npos) {
        std::cout << "[LLMSystemHTTP] Content has tool-like text but wasn't parsed:" << std::endl;
        std::cout << content.substr(0, 500) << std::endl;
    }
    std::cout << "[LLMSystemHTTP] Text response: " << (content.empty() ? "(empty)" : content.substr(0, 50)) << std::endl;

    return content;
}

std::string LLMSystemHTTP::parse_openai_response(const std::string& json) {
    // OpenAI format: {"choices":[{"message":{"content":"..."}}]}
    return json_extract_string(json, "content");
}

std::string LLMSystemHTTP::parse_anthropic_response(const std::string& json) {
    // Anthropic format: {"content":[{"text":"..."}]}
    return json_extract_string(json, "text");
}

// ============================================================================
// JSON Helpers (minimal, no library)
// ============================================================================

std::string LLMSystemHTTP::json_escape(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
        switch (c) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:   escaped << c; break;
        }
    }
    return escaped.str();
}

std::string LLMSystemHTTP::json_unescape(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c != '\\' || i + 1 >= str.size()) {
            out += c;
            continue;
        }
        char e = str[++i];
        switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'u': {
                if (i + 4 < str.size()) {
                    unsigned cp = 0;
                    bool ok = true;
                    for (int k = 1; k <= 4; ++k) {
                        char h = str[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else { ok = false; break; }
                    }
                    if (ok) {
                        i += 4;
                        // BMP -> UTF-8 (surrogate pairs pass through
                        // as-is; the LLM path doesn't emit them)
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                }
                out += 'u';  // malformed: keep something visible
                break;
            }
            default:
                out += e;   // unknown escape: keep the char
        }
    }
    return out;
}

std::string LLMSystemHTTP::json_extract_string(const std::string& json, const std::string& key) {
    // Find "key" then the opening quote of its value, tolerating
    // whitespace around the colon ("key": "value").
    std::string key_pat = "\"" + key + "\"";
    size_t start = std::string::npos;
    size_t at = 0;
    while ((at = json.find(key_pat, at)) != std::string::npos) {
        size_t i = at + key_pat.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                                   json[i] == '\n' || json[i] == '\r'))
            ++i;
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t' ||
                                       json[i] == '\n' || json[i] == '\r'))
                ++i;
            if (i < json.size() && json[i] == '"') {
                start = i + 1;
                break;
            }
        }
        at += key_pat.size();
    }

    if (start == std::string::npos) {
        return "";  // Key not found
    }
    size_t end = start;

    // Find the closing quote with a real escape-state scan. The old
    // prev-char check misread an escaped backslash before a genuine
    // closing quote (\\\") and ran past the value.
    bool escaped = false;
    while (end < json.length()) {
        char c = json[end];
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        }
        end++;
    }

    // The slice is a JSON-encoded string: DECODE it. Returning the
    // raw bytes double-encoded every Anthropic reply (literal \n and
    // \" reached the callers and broke their parsers).
    return json_unescape(json.substr(start, end - start));
}

// ============================================================================
// ReAct Iteration (Tool Calling Support)
// ============================================================================

std::string LLMSystemHTTP::build_admin_system_prompt(const std::string& user_command) {
#ifdef HAS_LLAMA
    if (!tool_registry_) {
        return "You are a helpful assistant.";
    }

    std::ostringstream oss;

    // System prompt follows Hermes-style pattern with strong negative constraints
    // Research: "Short constraints beat long essays" (OpenAI, 2025)
    // "Output ONLY" is more effective than "Be concise" for Qwen models
    // Triple negative constraints proven to reduce verbose explanations
    // See: docs/LLM_PROMPT_ENG.md for research background
    oss << "You are a function calling AI model. Available tools: ";

    // List tool names only (no descriptions, no schemas)
    auto tools = tool_registry_->get_all_tools();
    for (size_t i = 0; i < tools.size(); i++) {
        if (i > 0) oss << ", ";
        oss << tools[i].name;
    }

    oss << ".\n\n";

    // Add battlemap instructions if tool is available
    // This enables LLMs to use visual ASCII grids instead of coordinate math
    // Research: LLMs excel at ASCII art but struggle with coordinate calculations
    // Performance: 10 entities via battlemap = 3 tool calls vs 10 individual = 10 calls
    if (tool_registry_->has_tool("create_entities_from_battlemap")) {
        oss << "BATTLEMAP SCENE GENERATION (preferred for complex scenes):\n\n";
        oss << "When user requests a scene/forest/environment, use ASCII battlemap for visual layout:\n\n";

        oss << "1. Draw a 50×50 ASCII grid (1m per cell) showing entity placement\n";
        oss << "2. Use legend characters: T=oak, P=pine, W=willow, D=dead tree\n";
        oss << "   R=pebble, B=boulder, S=snake, b=butterfly, H=humanoid, L=light, C=cube\n";
        oss << "3. Use . or space for empty cells\n";
        oss << "4. Create entities iteratively by calling create_entities_from_battlemap\n\n";

        oss << "EXAMPLE BATTLEMAP (10x10 forest clearing):\n";
        oss << "```\n";
        oss << "..........\n";
        oss << ".T......P.\n";
        oss << "..........\n";
        oss << "....R.....\n";
        oss << "..........\n";
        oss << "...S......\n";
        oss << "..........\n";
        oss << ".T....T...\n";
        oss << "..........\n";
        oss << ".....L....\n";
        oss << "```\n\n";

        oss << "BATTLEMAP WORKFLOW:\n";
        oss << "1. Draw battlemap showing entity layout (up to 50×50 grid)\n";
        oss << "2. Call create_entities_from_battlemap with full battlemap string\n";
        oss << "3. Tool creates 5 entities per call, returns remaining count\n";
        oss << "4. Observe results: {\"entities_created\": [...], \"remaining\": N}\n";
        oss << "5. If remaining > 0, call again with next_batch_start value\n";
        oss << "6. STOP when remaining=0 or status=COMPLETE - do NOT call again\n\n";

        oss << "BATTLEMAP BENEFITS:\n";
        oss << "- Visual layout planning (you excel at ASCII art!)\n";
        oss << "- C++ handles coordinate math (guaranteed correctness)\n";
        oss << "- Automatic Z-coordinate assignment by entity type\n";
        oss << "- Easy to see spatial relationships and avoid overlaps\n\n";

        oss << "=== SCENE TEMPLATES ===\n\n";

        oss << "When user requests \"create scene X at Y,Z\", use these templates:\n\n";

        oss << "FOREST_CLEARING Template (50×50 battlemap, 1m resolution):\n";
        oss << "Goal: Natural forest clearing with trees around perimeter, open center\n\n";

        oss << "🔴 CRITICAL SIZE REQUIREMENT:\n";
        oss << "- You MUST generate a complete 50×50 battlemap in ONE tool call\n";
        oss << "- That means EXACTLY 50 lines of text\n";
        oss << "- Each line EXACTLY 50 characters (use dots . for empty cells)\n";
        oss << "- Total characters: 2500 cells + 49 newlines = 2549 chars\n";
        oss << "- DO NOT generate partial battlemaps (like 10×10)\n";
        oss << "- DO NOT split across multiple tool calls\n\n";

        oss << "Format Example (first 5 lines of 50):\n";
        oss << ".T.........P................................T.........\n";
        oss << "..................................................\n";
        oss << "........W..............................................\n";
        oss << "..................................................\n";
        oss << "....................R...............................\n";
        oss << "... (45 more lines like this, each 50 chars) ...\n\n";
        oss << "Layout Rules:\n";
        oss << "- Trees: 10-15 total, placed around perimeter (outer 15 cells)\n";
        oss << "- Variety: Mix of T (oak 40%), P (pine 30%), W (willow 30%)\n";
        oss << "- Spacing: 3-7 cells between trees (avoid dense clumps)\n";
        oss << "- Clearing: Center 20×20 cells remain empty (.)\n";
        oss << "- Wildlife: 1-2 butterflies (b) in clearing, 1 snake (S) on ground\n";
        oss << "- Rocks: 2-3 scattered (R=pebble or B=boulder)\n";
        oss << "- Lighting: 1 light source (L) in center\n\n";

        oss << "Battlemap Structure (50×50 grid):\n";
        oss << "- Rows 0-10: Northern treeline (sparse T, P, W)\n";
        oss << "- Rows 11-15: Transition zone (1-2 trees, rocks)\n";
        oss << "- Rows 16-34: Central clearing (mostly empty, wildlife, light)\n";
        oss << "- Rows 35-40: Transition zone (1-2 trees)\n";
        oss << "- Rows 41-49: Southern treeline (sparse T, P, W)\n";
        oss << "- Columns 0-10, 40-49: Western/Eastern treelines\n\n";

        oss << "🔴 When user requests \"create scene forest_clearing at X,Y\":\n";
        oss << "- IMMEDIATELY generate the COMPLETE 50×50 battlemap (all 50 lines!)\n";
        oss << "- Call create_entities_from_battlemap with the battlemap\n";
        oss << "- If remaining > 0, call again with batch_start until all entities created\n";
        oss << "- DO NOT use calculate_random_positions or create entities one-by-one\n";
        oss << "- The battlemap approach is faster and creates all entities at once\n\n";
    }

    oss << "Output ONLY <tool_call> blocks. No explanations. No reasoning. No text.\n\n";
    oss << "Format:\n";
    oss << "<tool_call>\n";
    oss << "{\"name\": \"tool_name\", \"arguments\": {\"param\": value}}\n";
    oss << "</tool_call>\n\n";
    oss << "Don't make assumptions. Don't explain. Don't describe.\n";

    return oss.str();
#else
    return "You are a helpful assistant.";
#endif
}

std::string LLMSystemHTTP::build_user_prompt_with_examples(const std::string& user_command) {
    // Add few-shot examples inline with user message (more effective for MLX)
    std::ostringstream oss;

    oss << "Examples:\n\n";
    oss << "User: Create a tree at 25,25\n";
    oss << "Assistant: <tool_call>\n";
    oss << "{\"name\": \"create_tree\", \"arguments\": {\"x\": 25.0, \"y\": 25.0}}\n";
    oss << "</tool_call>\n\n";

    oss << "User: Create a light at 30,30 with strength 50000\n";
    oss << "Assistant: <tool_call>\n";
    oss << "{\"name\": \"create_light\", \"arguments\": {\"x\": 30.0, \"y\": 30.0, \"strength\": 50000.0}}\n";
    oss << "</tool_call>\n\n";

    oss << "Now:\n\n";
    oss << "User: " << user_command << "\n";
    oss << "Assistant:";

    return oss.str();
}

std::string LLMSystemHTTP::generate_with_react_iteration(
    const std::string& initial_prompt,
    int max_tokens,
    const std::vector<Message>& input_messages,
    std::vector<Message>& output_messages,
    std::string& output_system_prompt
) {
    if (!tool_registry_) {
        return generate_internal(initial_prompt, max_tokens);
    }

    // Detect if this is first iteration (input_messages empty) or continuation (populated)
    bool is_continuation = !input_messages.empty();

    std::vector<Message> messages_for_request;
    std::string system_prompt;

    if (is_continuation) {
        // ================================================================
        // CONTINUATION ITERATION: Use existing message array
        // ================================================================
        std::cout << "[Worker] Continuation iteration - using message array (KV cache optimization)" << std::endl;

        // Use input_messages directly (already contains system + conversation history)
        messages_for_request = input_messages;

        // Extract system prompt from first message for output
        if (!input_messages.empty() && input_messages[0].role == "system") {
            system_prompt = input_messages[0].content;
        }

        // FULL PROMPT DUMP
        std::ofstream prompt_log("/tmp/llm_prompts.log", std::ios::app);
        prompt_log << "\n========== CONTINUATION ITERATION (MESSAGE ARRAY) ==========\n";
        prompt_log << "Message count: " << messages_for_request.size() << "\n";
        for (size_t i = 0; i < messages_for_request.size(); ++i) {
            prompt_log << "Message " << i << " [" << messages_for_request[i].role << "]: "
                      << messages_for_request[i].content.length() << " chars\n";
        }
        prompt_log.close();

        // Build request JSON with message array (enables MLX KV cache)
        std::string request_json;
        switch (config_.provider) {
            case LLMProvider::MLX:
                request_json = build_mlx_request_with_messages(messages_for_request, max_tokens);
                break;
            case LLMProvider::OpenAI:
                request_json = build_openai_request_with_messages(messages_for_request, max_tokens);
                break;
            case LLMProvider::Anthropic:
                request_json = build_anthropic_request_with_messages(messages_for_request, max_tokens);
                break;
            default:
                request_json = build_mlx_request_with_messages(messages_for_request, max_tokens);
        }

        // Make HTTP POST request
        HTTPResponse http_response = http_client_->post(
            config_.completion_path,
            request_json,
            "application/json",
            auth_headers_
        );

        if (http_response.status_code != 200) {
            std::cerr << "[LLMSystemHTTP] HTTP error " << http_response.status_code << std::endl;
            return "[ERROR: HTTP " + std::to_string(http_response.status_code) + "]";
        }

        std::string response = parse_response_json(http_response.body);

        // Populate output parameters for continuation tracking
        output_messages = messages_for_request;  // Continuation message array
        output_system_prompt = system_prompt;    // System prompt for all iterations

        return response;

    } else {
        // ================================================================
        // FIRST ITERATION: Build message array from scratch
        // ================================================================

        // Strip "root42:" prefix
        std::string user_command = initial_prompt.substr(7);
    // Trim leading whitespace
    size_t start = user_command.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        user_command = user_command.substr(start);
    }

    if (ENABLE_LLM_DEBUG_LOGS) {
        std::cout << "[LLMSystemHTTP] ReAct iteration starting with command: " << user_command << std::endl;
    }

    // Build minimal system prompt (tool names only)
    system_prompt = build_admin_system_prompt(user_command);

    std::cout << "[LLMSystemHTTP] System prompt (" << system_prompt.length() << " bytes):" << std::endl;
    std::cout << system_prompt << std::endl;
    std::cout << "[LLMSystemHTTP] End system prompt" << std::endl;

    // Build user prompt with few-shot examples inline
    std::string current_user_prompt = build_user_prompt_with_examples(user_command);

    std::cout << "\n========== ReAct Iteration 1/" << MAX_ITERATIONS << " ==========" << std::endl;
    std::cout << "[INSTRUMENTATION] Current prompt length: " << current_user_prompt.length() << " chars" << std::endl;

    // FULL PROMPT DUMP (first iteration)
    std::ofstream prompt_log_first("/tmp/llm_prompts.log", std::ios::app);
        prompt_log_first << "\n========== FIRST ITERATION PROMPTS ==========\n";
        prompt_log_first << "System prompt length: " << system_prompt.length() << " chars\n";
        prompt_log_first << "User prompt length: " << current_user_prompt.length() << " chars\n";
        prompt_log_first << "--- SYSTEM PROMPT START ---\n";
        prompt_log_first << system_prompt;
        prompt_log_first << "\n--- SYSTEM PROMPT END ---\n";
        prompt_log_first << "--- USER PROMPT START ---\n";
        prompt_log_first << current_user_prompt;
        prompt_log_first << "\n--- USER PROMPT END ---\n\n";
        prompt_log_first.close();

        // Build message array for first iteration (enables MLX KV cache)
        std::vector<Message> messages_for_request;
        messages_for_request.push_back({.role = "system", .content = system_prompt});
        messages_for_request.push_back({.role = "user", .content = current_user_prompt});

        // Build request JSON using message array format
        std::string request_json;
        switch (config_.provider) {
            case LLMProvider::MLX:
                request_json = build_mlx_request_with_messages(messages_for_request, max_tokens);
                break;
            case LLMProvider::OpenAI:
                request_json = build_openai_request_with_messages(messages_for_request, max_tokens);
                break;
            case LLMProvider::Anthropic:
                request_json = build_anthropic_request_with_messages(messages_for_request, max_tokens);
                break;
            default:
                request_json = build_mlx_request_with_messages(messages_for_request, max_tokens);
        }

        // FULL REQUEST DUMP (first iteration)
        std::ofstream req_log_first("/tmp/llm_prompts.log", std::ios::app);
        req_log_first << "\n========== FIRST ITERATION REQUEST JSON ==========\n";
        req_log_first << "Request size: " << request_json.length() << " bytes\n";
        req_log_first << "--- FULL REQUEST START ---\n";
        req_log_first << request_json;
        req_log_first << "\n--- FULL REQUEST END ---\n\n";
        req_log_first.close();

        if (ENABLE_LLM_DEBUG_LOGS) {
            std::cout << "[LLMSystemHTTP] Request JSON: " << request_json.substr(0, 200) << "..." << std::endl;
        }

        // Make HTTP POST request
        auto http_start = std::chrono::high_resolution_clock::now();
        HTTPResponse http_response = http_client_->post(
            config_.completion_path,
            request_json,
            "application/json",
            auth_headers_
        );
        auto http_end = std::chrono::high_resolution_clock::now();
        auto http_ms = std::chrono::duration_cast<std::chrono::milliseconds>(http_end - http_start).count();

        std::cout << "[INSTRUMENTATION] HTTP POST completed in " << http_ms << "ms" << std::endl;
        std::cout << "[INSTRUMENTATION] HTTP status: " << http_response.status_code << std::endl;
        std::cout << "[INSTRUMENTATION] Response body length: " << http_response.body.length() << " chars" << std::endl;

        if (http_response.status_code != 200) {
            std::cerr << "[LLMSystemHTTP] HTTP error " << http_response.status_code << std::endl;
            return "[ERROR: HTTP " + std::to_string(http_response.status_code) + "]";
        }

        std::string response = parse_response_json(http_response.body);

        std::cout << "[INSTRUMENTATION] Parsed response length: " << response.length() << " chars" << std::endl;
        std::cout << "[INSTRUMENTATION] Response content: " << response.substr(0, 200) << (response.length() > 200 ? "..." : "") << std::endl;

        if (ENABLE_LLM_DEBUG_LOGS) {
            std::cout << "[LLMSystemHTTP] Response: " << response << std::endl;
        }

        // Populate output parameters for continuation tracking
        output_messages = messages_for_request;  // Initial message array
        output_system_prompt = system_prompt;    // System prompt for all iterations

        return response;
    }
}

bool LLMSystemHTTP::has_tool_calls(const std::string& response) const {
    return response.find("TOOL_CALL:") != std::string::npos ||
           response.find("<tool_call>") != std::string::npos;
}

LLMSystemHTTP::ToolCall LLMSystemHTTP::parse_first_tool_call(const std::string& response) const {
    ToolCall result;

    // Parse Hermes XML format: <tool_call>{"name":"...","arguments":{...}}</tool_call>
    size_t start_tag = response.find("<tool_call>");
    size_t end_tag = response.find("</tool_call>");

    if (start_tag != std::string::npos && end_tag != std::string::npos) {
        size_t json_start = start_tag + 11;  // Skip "<tool_call>"
        std::string json_content = response.substr(json_start, end_tag - json_start);

        // Trim whitespace
        size_t first = json_content.find_first_not_of(" \n\r\t");
        size_t last = json_content.find_last_not_of(" \n\r\t");
        if (first != std::string::npos) {
            json_content = json_content.substr(first, last - first + 1);
        }

        // Extract name: "name":"value"
        size_t name_pos = json_content.find("\"name\":");
        if (name_pos != std::string::npos) {
            size_t name_start = json_content.find("\"", name_pos + 7) + 1;
            size_t name_end = json_content.find("\"", name_start);
            result.name = json_content.substr(name_start, name_end - name_start);

            // Extract arguments: everything from "arguments": to end of JSON object
            size_t args_pos = json_content.find("\"arguments\":", name_pos);
            if (args_pos != std::string::npos) {
                size_t args_start = json_content.find(":", args_pos) + 1;
                // Find the closing } that matches
                int brace_count = 0;
                size_t args_end = args_start;
                for (; args_end < json_content.length(); args_end++) {
                    if (json_content[args_end] == '{') {
                        brace_count++;
                    }
                    else if (json_content[args_end] == '}') {
                        brace_count--;
                        if (brace_count == 0) {
                            // Found the matching closing brace
                            args_end++;  // Include the closing brace
                            break;
                        }
                    }
                }
                result.args_json = json_content.substr(args_start, args_end - args_start);

                // Trim whitespace
                first = result.args_json.find_first_not_of(" \n\r\t");
                last = result.args_json.find_last_not_of(" \n\r\t");
                if (first != std::string::npos) {
                    result.args_json = result.args_json.substr(first, last - first + 1);
                }
            }
        }
    }

    return result;
}

std::string LLMSystemHTTP::format_tool_response(const std::string& tool_name,
                                                 const std::string& result) const {
    std::string response = "TOOL_RESULT: " + tool_name + " -> " + result;

    // If result contains NEXT_ACTION with a tool call instruction, remind LLM to output tool call
    if (result.find("NOW call") != std::string::npos) {
        response += "\n\nIMPORTANT: The above result requires you to call another tool. Output <tool_call> now!";
    }

    return response;
}

// Rebuild per-provider auth headers from the current config_.
// Called once at initialize() and whenever the provider config
// changes (today: only at init). MLX gets none; OpenAI gets
// Bearer; Anthropic gets x-api-key + the required version pin.
void LLMSystemHTTP::rebuild_auth_headers_() {
    auth_headers_.clear();
    switch (config_.provider) {
        case LLMProvider::OpenAI:
            if (!config_.api_key.empty()) {
                auth_headers_.emplace_back("Authorization",
                                           "Bearer " + config_.api_key);
            }
            break;
        case LLMProvider::Anthropic:
            if (!config_.api_key.empty()) {
                auth_headers_.emplace_back("x-api-key", config_.api_key);
            }
            // Anthropic requires a stable API version pin per the
            // /v1/messages contract. 2023-06-01 is the current
            // long-term version per docs.anthropic.com.
            auth_headers_.emplace_back("anthropic-version", "2023-06-01");
            break;
        case LLMProvider::MLX:
        case LLMProvider::Custom:
            // No auth by default. If a user runs MLX behind an
            // auth shim they can set api_key + use Custom; we
            // still pass it as Authorization: Bearer for that case.
            if (!config_.api_key.empty()) {
                auth_headers_.emplace_back("Authorization",
                                           "Bearer " + config_.api_key);
            }
            break;
    }
}

} // namespace Logosphere
