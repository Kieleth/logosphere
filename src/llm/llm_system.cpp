// Logosphere Engine - LLM System Implementation
//
// Wraps llama.cpp for text generation with async worker thread
// Based on working spike test (logosphere/tests/test_llm_spike.cpp)

#include "logosphere/llm/llm_system.h"
#include "tools/tool_registry.h"
#include "llama.h"
#include <cstdio>
#include <vector>
#include <iostream>
#include <sstream>

namespace Logosphere {

// Debug logging (set to false for clean LLM interaction logs)
static constexpr bool ENABLE_LLM_DEBUG_LOGS = false;

LLMSystem::LLMSystem()
    : model_(nullptr)
    , vocab_(nullptr)
    , context_(nullptr)
    , sampler_(nullptr)
    , max_context_size_(32768)  // Max out model's full capacity (Qwen2.5 supports 32K)
    , initialized_(false)
    , tool_registry_(nullptr)
{
}

LLMSystem::~LLMSystem() {
    shutdown();
}

void LLMSystem::cleanup() {
    if (sampler_) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (context_) {
        llama_free(context_);
        context_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    vocab_ = nullptr;
    initialized_ = false;
}

// ============================================================================
// Tool Calling
// ============================================================================

void LLMSystem::register_tool_registry(ToolRegistry* registry) {
    tool_registry_ = registry;
    if (registry) {
        std::cout << "[LLMSystem] Tool registry registered (function calling enabled)" << std::endl;
    } else {
        std::cout << "[LLMSystem] Tool registry cleared (function calling disabled)" << std::endl;
    }
}

// Set narrative system prompt (DungeonMaster personality)
void LLMSystem::set_narrative_system_prompt(const std::string& prompt) {
    narrative_system_prompt_ = prompt;
    std::cout << "[LLMSystem] Narrative system prompt set (" << prompt.length() << " chars)" << std::endl;
}

// Set scene context callback (provides current game state)
void LLMSystem::set_scene_context_callback(SceneContextCallback callback) {
    scene_context_callback_ = callback;
    std::cout << "[LLMSystem] Scene context callback registered" << std::endl;
}

// Set trace configuration
void LLMSystem::set_trace_config(const LLMTraceConfig& config) {
    trace_config_ = config;
    std::cout << "[LLMSystem] Trace config updated: tracing="
              << (config.enable_tracing ? "ON" : "OFF") << std::endl;
}

// ============================================================================
// Initialization
// ============================================================================

bool LLMSystem::initialize(const std::string& model_path) {
    std::cout << "[LLMSystem] Initializing with model: " << model_path << std::endl;

    // Load Metal/CUDA backends
    ggml_backend_load_all();

    // Model parameters - FULL GPU MODE with no_host (bypass host buffers)
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;  // All layers on GPU
    model_params.no_host = true;     // Bypass host buffers (unified memory optimization)

    // Load model
    model_ = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model_) {
        last_error_ = "Failed to load model from: " + model_path;
        std::cerr << "[LLMSystem] ERROR: " << last_error_ << std::endl;
        return false;
    }

    std::cout << "[LLMSystem] Model loaded successfully" << std::endl;

    // Get vocabulary
    vocab_ = llama_model_get_vocab(model_);
    if (!vocab_) {
        last_error_ = "Failed to get vocabulary from model";
        cleanup();
        return false;
    }

    // Context parameters
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = max_context_size_;
    ctx_params.n_batch = 8192;  // Large enough for admin prompts with tool definitions + spatial workflow (1640+ tokens)
    ctx_params.no_perf = false;  // Enable performance stats

    // Create context
    context_ = llama_init_from_model(model_, ctx_params);
    if (!context_) {
        last_error_ = "Failed to create context";
        cleanup();
        return false;
    }

    std::cout << "[LLMSystem] Context created (n_ctx=" << max_context_size_ << ")" << std::endl;

    // Initialize sampler chain with temperature=0 (fully deterministic)
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    sampler_ = llama_sampler_chain_init(sparams);

    // Add temperature sampler with temp=0 for maximum determinism
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.0f));

    // Add greedy sampler (always picks highest probability)
    llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());

    std::cout << "[LLMSystem] Sampler initialized (greedy)" << std::endl;

    // Warm-up: Run a realistic prompt through the system to trigger lazy initialization
    // This eliminates the 700ms first-token sampling delay on first user prompt
    std::cout << "[LLMSystem] Running warm-up prompt to initialize GPU kernels..." << std::endl;
    auto warmup_start = std::chrono::high_resolution_clock::now();

    // Use a longer prompt to ensure GPU kernels are properly triggered
    const char* warmup_prompt = "The quick brown fox jumps over the lazy dog. This is a warm-up prompt to initialize GPU kernels and sampler state before the first user prompt.";
    int warmup_token_count = llama_tokenize(vocab_, warmup_prompt, strlen(warmup_prompt),
                                             nullptr, 0, true, false);
    if (warmup_token_count > 0) {
        std::vector<llama_token> warmup_tokens(warmup_token_count);
        llama_tokenize(vocab_, warmup_prompt, strlen(warmup_prompt),
                      warmup_tokens.data(), warmup_tokens.size(), true, false);

        llama_batch warmup_batch = llama_batch_get_one(warmup_tokens.data(), warmup_tokens.size());
        llama_decode(context_, warmup_batch);

        // Sample 3 tokens to fully trigger sampler initialization
        for (int i = 0; i < 3; i++) {
            llama_token token = llama_sampler_sample(sampler_, context_, -1);
            llama_batch next_batch = llama_batch_get_one(&token, 1);
            llama_decode(context_, next_batch);
        }

        // Synchronize GPU to ensure all operations complete
        llama_synchronize(context_);
    }

    auto warmup_end = std::chrono::high_resolution_clock::now();
    double warmup_ms = std::chrono::duration<double, std::milli>(warmup_end - warmup_start).count();
    std::cout << "[LLMSystem] Warm-up completed in " << warmup_ms << "ms (GPU kernels initialized)" << std::endl;

    initialized_ = true;

    // Start worker thread
    shutdown_requested_.store(false, std::memory_order_release);
    worker_thread_ = std::thread(&LLMSystem::worker_thread_main, this);

    std::cout << "[LLMSystem] Worker thread started (async mode enabled)" << std::endl;

    return true;
}

void LLMSystem::shutdown() {
    if (!initialized_) {
        return;
    }

    std::cout << "[LLMSystem] Shutting down..." << std::endl;

    // Signal worker thread to stop
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        shutdown_requested_.store(true, std::memory_order_release);
    }
    request_cv_.notify_one();

    // Wait for worker thread to finish
    if (worker_thread_.joinable()) {
        std::cout << "[LLMSystem] Waiting for worker thread..." << std::endl;
        worker_thread_.join();
        std::cout << "[LLMSystem] Worker thread stopped" << std::endl;
    }

    // Cleanup llama.cpp resources
    cleanup();

    std::cout << "[LLMSystem] Shutdown complete" << std::endl;
}

// ============================================================================
// ASYNC API IMPLEMENTATION
// ============================================================================

uint64_t LLMSystem::submit_request(const std::string& prompt,
                                   int max_tokens,
                                   LLMCallback callback,
                                   void* user_data) {
    std::cout << "[LLM_SUBMIT_DEBUG] submit_request called with prompt: " << prompt.substr(0, 50) << "..." << std::endl;

    if (!initialized_) {
        std::cerr << "[LLMSystem] ERROR: Cannot submit request, not initialized" << std::endl;
        return 0;
    }

    // Generate unique request ID
    uint64_t request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    std::cout << "[LLM_SUBMIT_DEBUG] Generated request_id: " << request_id << std::endl;

    // Detect admin mode (root42: prefix)
    bool is_admin_mode = (prompt.rfind("root42:", 0) == 0);
    std::string actual_prompt = prompt;
    std::string original_prompt = prompt;
    std::cout << "[LLM_SUBMIT_DEBUG] is_admin_mode: " << is_admin_mode << std::endl;

    if (is_admin_mode && tool_registry_) {
        std::cout << "[ROOT42_DEBUG] Entering admin mode path" << std::endl;

        // Strip root42: prefix
        std::string admin_command = prompt.substr(7);
        std::cout << "[ROOT42_DEBUG] Stripped command: " << admin_command << std::endl;

        // Build admin system prompt with tool descriptions
        std::cout << "[ROOT42_DEBUG] Building admin prompt..." << std::endl;

        // Defensive check: verify tool_registry_ is valid
        if (!tool_registry_) {
            std::cerr << "[ROOT42_DEBUG] ERROR: tool_registry_ is NULL!" << std::endl;
            last_error_ = "Tool registry is NULL";
            return 0;
        }

        std::cout << "[ROOT42_DEBUG] tool_registry_ pointer=" << (void*)tool_registry_ << std::endl;

        // Qwen2.5 Hermes-style chat template format
        std::ostringstream oss;
        oss << "<|im_start|>system\n";
        oss << "You are a function-calling assistant. Execute the user's command using available tools.\n\n";
        oss << "CRITICAL OUTPUT FORMAT:\n";
        oss << "- Output ONLY the <tool_call> XML tags with JSON inside\n";
        oss << "- DO NOT write Python code, explanations, or markdown\n";
        oss << "- DO NOT import modules or write scripts\n";
        oss << "- The tool is ALREADY AVAILABLE - just call it with XML format\n\n";

        oss << "SPATIAL AWARENESS (ALWAYS CALL FIRST):\n";
        oss << "When user asks to create entities/scenes, FIRST call get_camera_view_context().\n";
        oss << "This tells you where the camera is looking and what chunk to create entities in.\n";
        oss << "Use the returned chunk_bounds to place entities in the visible area.\n\n";

        oss << "SPATIAL WORKFLOW:\n";
        oss << "1. No position specified: Call find_placement_spot(size) first, use returned coords\n";
        oss << "2. User says \"at me\"/\"nearby\": Call get_controlled_position() first\n";
        oss << "   - If has_entity=false: Use find_placement_spot() and set_input_target()\n";
        oss << "   - If has_entity=true: Use position + offset\n";
        oss << "3. Exact coordinates given: Use them directly\n\n";

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
        oss << "5. Call again with batch_start parameter until remaining=0\n\n";

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

        oss << "EXAMPLES:\n\n";

        oss << "Example 1: \"create a cube\" (no position specified)\n";
        oss << "Best practice: Call find_placement_spot first for collision-checked placement\n";
        oss << "<tool_call>\n";
        oss << "{\"name\": \"find_placement_spot\", \"arguments\": {\"size\": 2.0}}\n";
        oss << "</tool_call>\n";
        oss << "Note: If position not specified, defaults to camera center (25,25) for visibility.\n\n";

        oss << "Example 2: \"create a cube at (20, 22, 1.5)\"\n";
        oss << "Exact coordinates provided - use them directly:\n";
        oss << "<tool_call>\n";
        oss << "{\"name\": \"create_cube\", \"arguments\": {\"x\": 20.0, \"y\": 22.0, \"z\": 1.5, \"size\": 2.0}}\n";
        oss << "</tool_call>\n\n";

        oss << "CRITICAL: Prefer calling find_placement_spot when user doesn't specify position.\n";
        oss << "It provides collision checking and optimal camera-visible placement.\n\n";

        std::cout << "[ROOT42_DEBUG] About to call tool_registry_->to_prompt_format()..." << std::endl;

        std::string tool_format;
        try {
            std::cout << "[ROOT42_DEBUG] Calling to_prompt_format() NOW..." << std::endl;
            tool_format = tool_registry_->to_prompt_format();
            std::cout << "[ROOT42_DEBUG] to_prompt_format() returned successfully (length=" << tool_format.length() << ")" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[ROOT42_DEBUG] CAUGHT EXCEPTION in to_prompt_format: " << e.what() << std::endl;
            last_error_ = std::string("Tool format exception: ") + e.what();
            return 0;
        } catch (...) {
            std::cout << "[ROOT42_DEBUG] CAUGHT UNKNOWN EXCEPTION in to_prompt_format" << std::endl;
            last_error_ = "Unknown exception in tool format";
            return 0;
        }

        std::cout << "[ROOT42_DEBUG] About to append tool_format to oss..." << std::endl;
        oss << tool_format;  // Tool format now includes proper formatting
        std::cout << "[ROOT42_DEBUG] Appended tool_format" << std::endl;

        // Close system message with few-shot examples (highest impact for format compliance)
        oss << "\n=== OUTPUT FORMAT EXAMPLES ===\n\n";
        oss << "CRITICAL: Output ONLY the XML tags. NO Python code, NO explanations, NO markdown.\n\n";
        oss << "User: Create a battlemap with trees and rocks\n";
        oss << "Assistant: <tool_call>\n";
        oss << R"({"name": "create_entities_from_battlemap", "arguments": {"battlemap": "...........\n.T..T..T..\n...........", "resolution": "10m", "origin_x": 0, "origin_y": 0}})" << "\n";
        oss << "</tool_call>\n\n";
        oss << "Example 2 - Simple entity:\n";
        oss << "User: Create a tree at (10, 5, 0)\n";
        oss << "Assistant: <tool_call>\n";
        oss << R"({"name": "create_tree", "arguments": {"x": 10, "y": 5, "z": 0, "spec": "oak"}})" << "\n";
        oss << "</tool_call>\n\n";
        oss << "WRONG - DO NOT DO THIS:\n";
        oss << "```python\n";
        oss << "from create_entities_from_battlemap import create_entities_from_battlemap\n";
        oss << "battlemap = \"...\"\n";
        oss << "```\n";
        oss << "Tools are already available - just call them with <tool_call> XML!\n\n";
        oss << "<|im_end|>\n";

        // User message
        oss << "<|im_start|>user\n";
        oss << admin_command << "\n";
        oss << "<|im_end|>\n";

        // Assistant message (model continues from here)
        oss << "<|im_start|>assistant\n";

        actual_prompt = oss.str();
        std::cout << "[ROOT42_DEBUG] Admin prompt built (length=" << actual_prompt.length() << ")" << std::endl;
    } else if (!narrative_system_prompt_.empty()) {
        // NARRATIVE MODE: Build DungeonMaster-style prompt with scene context
        std::ostringstream oss;

        // System prompt with personality
        oss << "<|im_start|>system\n";
        oss << narrative_system_prompt_;

        // Inject scene context if callback is set
        if (scene_context_callback_) {
            oss << "\n\n";
            oss << scene_context_callback_();
        }
        oss << "<|im_end|>\n";

        // User message
        oss << "<|im_start|>user\n";
        oss << prompt << "\n";
        oss << "<|im_end|>\n";

        // Assistant message (model continues from here)
        oss << "<|im_start|>assistant\n";

        actual_prompt = oss.str();
        std::cout << "[NARRATIVE_DEBUG] Narrative prompt built (length=" << actual_prompt.length() << ")" << std::endl;
    }

    // Create request
    LLMRequest request;
    request.prompt = actual_prompt;
    request.max_tokens = max_tokens;
    request.callback = callback;
    request.user_data = user_data;
    request.request_id = request_id;
    request.is_admin_mode = is_admin_mode;
    request.original_prompt = original_prompt;

    // Enqueue request (thread-safe)
    std::cout << "[LLM_SUBMIT_DEBUG] Enqueueing request " << request_id << " to worker thread" << std::endl;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        request_queue_.push(request);
    }
    std::cout << "[LLM_SUBMIT_DEBUG] Request " << request_id << " enqueued, queue size now: " << request_queue_.size() << std::endl;

    // Wake worker thread
    std::cout << "[LLM_SUBMIT_DEBUG] Notifying worker thread..." << std::endl;
    request_cv_.notify_one();
    std::cout << "[LLM_SUBMIT_DEBUG] Worker thread notified, returning request_id: " << request_id << std::endl;

    return request_id;
}

void LLMSystem::cancel_request(uint64_t request_id) {
    std::lock_guard<std::mutex> lock(cancel_mutex_);
    cancelled_requests_.insert(request_id);
}

bool LLMSystem::process_completed_responses() {
    if (!initialized_) {
        return false;
    }

    bool any_processed = false;

    // Get all completed responses (thread-safe)
    std::queue<LLMResponse> responses_to_process;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        std::swap(responses_to_process, response_queue_);
    }

    // Invoke callbacks (no locks held)
    while (!responses_to_process.empty()) {
        LLMResponse& response = responses_to_process.front();

        // Invoke callback if not cancelled
        if (!response.cancelled && response.callback) {
            // NOTE: Tool execution now happens during ReAct iteration in generate_with_react_iteration()
            // No need to parse/execute tools here - just pass response directly to callback
            std::string final_response = response.response;

            // Invoke callback with response from ReAct iteration
            response.callback(response.original_prompt, final_response, response.user_data);
            any_processed = true;
        }

        responses_to_process.pop();
    }

    return any_processed;
}

bool LLMSystem::has_pending_requests() const {
    std::lock_guard<std::mutex> lock(request_mutex_);
    return !request_queue_.empty();
}

int LLMSystem::get_queue_size() const {
    std::lock_guard<std::mutex> lock(request_mutex_);
    return static_cast<int>(request_queue_.size());
}

// ============================================================================
// SYNC API (backward compatibility, uses async internally)
// ============================================================================

std::string LLMSystem::generate(const std::string& prompt, int max_tokens) {
    if (!initialized_) {
        last_error_ = "LLM not initialized";
        return "";
    }

    // Use async API internally, block until complete
    std::string result;
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done = false;

    submit_request(prompt, max_tokens,
        [&](const std::string&, const std::string& resp, void*) {
            result = resp;
            {
                std::lock_guard<std::mutex> lock(done_mutex);
                done = true;
            }
            done_cv.notify_one();
        },
        nullptr);

    // Block until callback invoked
    // Note: process_completed_responses() must be called externally
    // This is a simplified implementation for backward compatibility
    // In practice, games should use async API directly
    std::unique_lock<std::mutex> lock(done_mutex);
    done_cv.wait(lock, [&]{ return done; });

    return result;
}

// ============================================================================
// WORKER THREAD IMPLEMENTATION
// ============================================================================

void LLMSystem::worker_thread_main() {
    std::cout << "[LLMSystem Worker] Thread started" << std::endl;

    while (true) {
        // Wait for request or shutdown signal
        LLMRequest request;
        bool has_request = false;

        {
            std::unique_lock<std::mutex> lock(request_mutex_);

            // Wait for request or shutdown
            request_cv_.wait(lock, [this]{
                return !request_queue_.empty() || shutdown_requested_.load(std::memory_order_acquire);
            });

            // Check shutdown
            if (shutdown_requested_.load(std::memory_order_acquire)) {
                std::cout << "[LLMSystem Worker] Shutdown requested, exiting" << std::endl;
                break;
            }

            // Get request
            if (!request_queue_.empty()) {
                request = request_queue_.front();
                request_queue_.pop();
                has_request = true;
            }
        }

        // Process request (no locks held)
        if (has_request) {
            // Check if cancelled
            bool cancelled = false;
            {
                std::lock_guard<std::mutex> lock(cancel_mutex_);
                if (cancelled_requests_.count(request.request_id) > 0) {
                    cancelled = true;
                    cancelled_requests_.erase(request.request_id);
                }
            }

            LLMResponse response;
            response.prompt = request.prompt;
            response.callback = request.callback;
            response.user_data = request.user_data;
            response.request_id = request.request_id;
            response.cancelled = cancelled;
            response.error = false;
            response.is_admin_mode = request.is_admin_mode;
            response.original_prompt = request.original_prompt;

            if (!cancelled) {
                // Generate response
                std::cout << "[LLMSystem Worker] Processing request " << request.request_id << std::endl;
                std::cout << "[ROOT42_DEBUG] Worker: is_admin_mode=" << request.is_admin_mode << std::endl;

                try {
                    // Use ReAct iteration if tools are available (both admin and regular mode)
                    if (tool_registry_) {
                        std::cout << "[ROOT42_DEBUG] Worker: Using ReAct iteration (tools available)" << std::endl;
                        response.response = generate_with_react_iteration(request.prompt, request.max_tokens);
                    } else {
                        std::cout << "[ROOT42_DEBUG] Worker: Using standard generation (no tools)" << std::endl;
                        response.response = generate_internal(request.prompt, request.max_tokens);
                    }

                    std::cout << "[ROOT42_DEBUG] Worker: Generation returned (length=" << response.response.length() << ")" << std::endl;
                    std::cout << "[LLMSystem Worker] Request " << request.request_id << " completed" << std::endl;
                } catch (const std::exception& e) {
                    response.error = true;
                    response.error_message = e.what();
                    std::cerr << "[LLMSystem Worker] ERROR: " << e.what() << std::endl;
                } catch (...) {
                    response.error = true;
                    response.error_message = "Unknown error during generation";
                    std::cerr << "[LLMSystem Worker] ERROR: Unknown exception" << std::endl;
                }
            } else {
                std::cout << "[LLMSystem Worker] Request " << request.request_id << " was cancelled" << std::endl;
            }

            // Enqueue response (thread-safe)
            {
                std::lock_guard<std::mutex> lock(response_mutex_);
                response_queue_.push(response);
            }
        }
    }

    std::cout << "[LLMSystem Worker] Thread exiting" << std::endl;
}

// ============================================================================
// REACT ITERATION WRAPPER (worker thread only, not thread-safe)
// ============================================================================

std::string LLMSystem::generate_with_react_iteration(const std::string& initial_prompt, int max_tokens) {
    const int MAX_ITERATIONS = 5;
    std::string full_context = initial_prompt;  // Full conversation context
    size_t prev_len = 0;  // Length of context already in KV cache
    std::string accumulated_response;

    for (int iteration = 0; iteration < MAX_ITERATIONS; iteration++) {
        // CRITICAL: Only decode NEW content (delta) to continue KV cache
        // Re-decoding the full context causes "failed to initialize batch" error
        std::string delta = full_context.substr(prev_len);

        if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
            std::cout << "[REACT] Iteration " << iteration + 1
                      << " - Context: " << full_context.length() << " chars"
                      << " | Cached: " << prev_len << " chars"
                      << " | Delta: " << delta.length() << " chars" << std::endl;
        }

        // Generate response for the delta (llama.cpp continues KV cache automatically)
        std::string response = generate_internal(delta, max_tokens);

        // Check for tool calls
        if (!has_tool_calls(response) || !tool_registry_) {
            // No tool calls or no registry - return response and stop
            accumulated_response += response;

            if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
                std::cout << "[REACT] Iteration " << iteration + 1
                          << " - No tool calls, stopping" << std::endl;
            }
            break;
        }

        // Parse first tool call
        auto tool_call = parse_first_tool_call(response);
        if (!tool_call.is_valid()) {
            // Invalid tool call - return what we have and stop
            accumulated_response += response;

            if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
                std::cout << "[REACT] Iteration " << iteration + 1
                          << " - Invalid tool call, stopping" << std::endl;
            }
            break;
        }

        // Log tool call
        if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
            std::cout << "[REACT] Iteration " << iteration + 1
                      << " - Tool call: " << tool_call.name << std::endl;
        }

        // Execute tool (NOTE: Tool execution from worker thread - tools must be thread-safe)
        std::string tool_result;
        try {
            // Parse args_json string back to JSON object for execute_tool()
            nlohmann::json args = nlohmann::json::parse(tool_call.args_json);
            tool_result = tool_registry_->execute_tool(tool_call.name, args);

            if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
                std::cout << "[REACT] Tool result: " << tool_result << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[REACT] Tool execution failed: " << e.what() << std::endl;
            tool_result = "{\"error\": \"Tool execution failed: " + std::string(e.what()) + "\"}";
        }

        // Format tool response for LLM
        std::string tool_response = format_tool_response(tool_call.name, tool_result);

        // Accumulate the LLM response (with tool tags for final output)
        accumulated_response += response + "\n";

        // Strip <tool_call> XML from response before adding to conversation history
        // (reduces prompt bloat - LLM doesn't need to see the tool call XML again)
        std::string response_without_tools;
        size_t pos = 0;
        while (pos < response.length()) {
            size_t tool_start = response.find("<tool_call>", pos);
            if (tool_start == std::string::npos) {
                response_without_tools += response.substr(pos);
                break;
            }
            response_without_tools += response.substr(pos, tool_start - pos);
            size_t tool_end = response.find("</tool_call>", tool_start);
            if (tool_end != std::string::npos) {
                pos = tool_end + 12;
            } else {
                pos = response.length();
            }
        }

        // Extend context for next iteration using Qwen chat template
        // Pattern: Add assistant response → mark as cached → add tool turn (to be decoded next)

        // Add assistant response and close turn
        full_context = full_context + response_without_tools + "\n<|im_end|>\n";
        prev_len = full_context.length();  // Mark everything up to here as cached

        // Add tool response as new user turn (this will be decoded in next iteration)
        full_context += "<|im_start|>user\n" + tool_response + "\n<|im_end|>\n";
        full_context += "<|im_start|>assistant\n";

        if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
            std::cout << "[REACT] Extended context, continuing generation..." << std::endl;
        }
    }

    if (trace_config_.enable_tracing && trace_config_.log_tool_calls) {
        std::cout << "[REACT] Iteration complete, total response length: "
                  << accumulated_response.length() << " chars" << std::endl;
    }

    // Strip out <tool_call> tags from user-facing response
    // Keep only the non-tool-call content (explanatory text, etc.)
    std::string cleaned_response;
    size_t pos = 0;
    while (pos < accumulated_response.length()) {
        size_t tool_start = accumulated_response.find("<tool_call>", pos);
        if (tool_start == std::string::npos) {
            // No more tool calls, append rest
            cleaned_response += accumulated_response.substr(pos);
            break;
        }

        // Append text before tool call
        cleaned_response += accumulated_response.substr(pos, tool_start - pos);

        // Skip to end of tool call
        size_t tool_end = accumulated_response.find("</tool_call>", tool_start);
        if (tool_end != std::string::npos) {
            pos = tool_end + 12;  // Skip "</tool_call>"
        } else {
            pos = accumulated_response.length();  // Malformed, skip to end
        }
    }

    // Trim whitespace
    cleaned_response.erase(0, cleaned_response.find_first_not_of(" \t\n\r"));
    cleaned_response.erase(cleaned_response.find_last_not_of(" \t\n\r") + 1);

    // If response is empty, provide a summary of what was done
    if (cleaned_response.empty()) {
        cleaned_response = "[Tools executed successfully]";
    }

    return cleaned_response;
}

// ============================================================================
// INTERNAL GENERATION (worker thread only, not thread-safe)
// ============================================================================

std::string LLMSystem::generate_internal(const std::string& prompt, int max_tokens) {
    // NOTE: This function is called ONLY by worker thread
    // llama.cpp context is not thread-safe, so no synchronization needed

    auto start_time = std::chrono::high_resolution_clock::now();
    auto phase_start = start_time;

    std::cout << "[LLM_PERF] === GENERATION START ===" << std::endl;
    std::cout << "[ROOT42_DEBUG] generate_internal: Starting (prompt_len=" << prompt.length() << " max_tokens=" << max_tokens << ")" << std::endl;

    // LLM TRACE: Request start
    if (trace_config_.enable_tracing) {
        std::cout << "[LLM_TRACE] === REQUEST START ===" << std::endl;
        std::cout << "[LLM_TRACE] Prompt length: " << prompt.length() << " chars" << std::endl;
        std::cout << "[LLM_TRACE] Max tokens: " << max_tokens << std::endl;

        if (trace_config_.log_prompts) {
            std::cout << "[LLM_TRACE_PROMPT] --- FULL PROMPT ---" << std::endl;
            std::cout << prompt << std::endl;
            std::cout << "[LLM_TRACE_PROMPT] --- END PROMPT ---" << std::endl;
        }
    }

    // Tokenize prompt
    phase_start = std::chrono::high_resolution_clock::now();
    std::cout << "[ROOT42_DEBUG] generate_internal: Tokenizing..." << std::endl;
    const int n_prompt = -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), NULL, 0, true, true);
    if (n_prompt <= 0) {
        std::cout << "[ROOT42_DEBUG] generate_internal: FAILED tokenization" << std::endl;
        throw std::runtime_error("Failed to tokenize prompt");
    }
    std::cout << "[ROOT42_DEBUG] generate_internal: Tokenized to " << n_prompt << " tokens" << std::endl;

    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab_, prompt.c_str(), prompt.size(),
                       prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        throw std::runtime_error("Tokenization failed");
    }

    auto tokenize_end = std::chrono::high_resolution_clock::now();
    double tokenize_ms = std::chrono::duration<double, std::milli>(tokenize_end - phase_start).count();
    if (trace_config_.enable_tracing && trace_config_.log_timing) {
        std::cout << "[LLM_TIMING] Tokenization: " << static_cast<int>(tokenize_ms) << "ms" << std::endl;
    }

    // Decode prompt in CHUNKS with GPU yielding to prevent long stalls
    phase_start = std::chrono::high_resolution_clock::now();
    std::cout << "[DECODE_TIMING] Starting CHUNKED prompt decode for " << n_prompt << " tokens..." << std::endl;
    auto decode_start = std::chrono::high_resolution_clock::now();

    const int CHUNK_SIZE = 64; // Process 64 tokens at a time, then yield GPU
    for (int chunk_start = 0; chunk_start < n_prompt; chunk_start += CHUNK_SIZE) {
        int chunk_size = std::min(CHUNK_SIZE, n_prompt - chunk_start);

        llama_batch batch = llama_batch_get_one(prompt_tokens.data() + chunk_start, chunk_size);

        if (llama_decode(context_, batch) != 0) {
            throw std::runtime_error("Failed to decode prompt chunk");
        }

        // CRITICAL: Synchronize BEFORE sleep to drain GPU queue
        // Without this, GPU work accumulates across ALL chunks!
        llama_synchronize(context_);

        // Yield GPU after each chunk (except last) - 50ms sleep for smoother rendering
        // Rendering can now use GPU during this sleep window
        if (chunk_start + chunk_size < n_prompt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    auto decode_cpu_end = std::chrono::high_resolution_clock::now();
    double decode_cpu_ms = std::chrono::duration<double, std::milli>(decode_cpu_end - decode_start).count();

    // REMOVED: Redundant explicit sync - llama_sampler_sample() calls llama_get_logits() which syncs
    // llama_synchronize(context_);

    auto decode_end = std::chrono::high_resolution_clock::now();
    double decode_ms = std::chrono::duration<double, std::milli>(decode_end - decode_start).count();
    std::cout << "[DECODE_TIMING] llama_decode CPU=" << decode_cpu_ms << "ms GPU_sync=" << (decode_ms - decode_cpu_ms) << "ms total=" << decode_ms << "ms" << std::endl;

    if (trace_config_.enable_tracing && trace_config_.log_timing) {
        std::cout << "[LLM_TIMING] Prompt decode (" << n_prompt << " tokens): " << static_cast<int>(decode_ms) << "ms" << std::endl;
    }

    // Generate tokens - PIPELINED: Start decode(i+1) before sampling(i)
    std::string response;
    int n_generated = 0;
    phase_start = std::chrono::high_resolution_clock::now();

    std::cout << "[GENERATION_TIMING] Starting PIPELINED token generation loop..." << std::endl;

    // First token: sample from prompt decode (GPU already working from line 845)
    llama_token current_token = 0;

    for (int i = 0; i < max_tokens; i++) {
        if (i % 10 == 0) {
            auto loop_time = std::chrono::high_resolution_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(loop_time - phase_start).count();
            std::cout << "[ROOT42_DEBUG] generate_internal: Generated " << i << " tokens so far (elapsed: " << elapsed_ms << "ms)..." << std::endl;
        }

        // YIELD GPU: Every 3 tokens, sleep 50ms to let rendering use GPU
        if (i > 0 && i % 3 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Sample token i (blocks for decode(i-1), but decode(i) will start immediately after)
        auto sample_start = std::chrono::high_resolution_clock::now();
        llama_token new_token = llama_sampler_sample(sampler_, context_, -1);
        auto sample_end = std::chrono::high_resolution_clock::now();
        double sample_ms = std::chrono::duration<double, std::milli>(sample_end - sample_start).count();

        // Check for end of generation
        if (llama_vocab_is_eog(vocab_, new_token)) {
            break;
        }

        // Start decode(i+1) IMMEDIATELY - GPU works while we process text
        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        auto token_decode_start = std::chrono::high_resolution_clock::now();
        if (llama_decode(context_, next_batch)) {
            throw std::runtime_error("Failed to decode token");
        }
        auto token_decode_end = std::chrono::high_resolution_clock::now();
        double token_decode_ms = std::chrono::duration<double, std::milli>(token_decode_end - token_decode_start).count();

        // Now process token i (GPU working on i+1 in parallel)
        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf), 0, true);
        if (n < 0) {
            throw std::runtime_error("Failed to convert token to text");
        }

        response.append(buf, n);

        // LLM TRACE: Streaming tokens (very verbose, disabled by default)
        if (trace_config_.enable_tracing && trace_config_.log_streaming) {
            // Print token text with escaping for readability
            std::string token_str(buf, n);
            // Escape newlines and special chars for log readability
            std::string escaped;
            for (char c : token_str) {
                if (c == '\n') escaped += "\\n";
                else if (c == '\t') escaped += "\\t";
                else if (c == '\r') escaped += "\\r";
                else escaped += c;
            }
            std::cout << "[LLM_STREAM] Token " << n_generated + 1
                      << ": \"" << escaped << "\" | Response: " << response.length() << " chars"
                      << std::endl;
        }

        // Log first 3 tokens timing
        if (i < 3) {
            std::cout << "[TOKEN_TIMING] Token " << i << ": sample=" << sample_ms << "ms decode=" << token_decode_ms << "ms (pipelined)" << std::endl;
        }

        n_generated++;
    }

    auto generate_end = std::chrono::high_resolution_clock::now();
    double generate_ms = std::chrono::duration<double, std::milli>(generate_end - phase_start).count();
    if (trace_config_.enable_tracing && trace_config_.log_timing) {
        std::cout << "[LLM_TIMING] Token generation (" << n_generated << " tokens): " << static_cast<int>(generate_ms) << "ms";
        if (n_generated > 0) {
            std::cout << " (" << static_cast<int>(generate_ms / n_generated) << "ms/token)";
        }
        std::cout << std::endl;
    }

    // Note: llama_batch_get_one returns a view, no cleanup needed

    std::cout << "[ROOT42_DEBUG] generate_internal: Completed! Generated " << n_generated << " tokens, response length=" << response.length() << std::endl;

    // LLM TRACE: Summary
    if (trace_config_.enable_tracing) {
        auto end_time = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        std::cout << "[LLM_TRACE] === GENERATION COMPLETE ===" << std::endl;
        std::cout << "[LLM_TRACE] Prompt tokens: " << n_prompt << std::endl;
        std::cout << "[LLM_TRACE] Generated tokens: " << n_generated << std::endl;
        std::cout << "[LLM_TRACE] Total time: " << static_cast<int>(total_ms) << "ms" << std::endl;

        if (n_generated > 0) {
            double tokens_per_sec = (n_generated / total_ms) * 1000.0;
            std::cout << "[LLM_TRACE] Speed: " << static_cast<int>(tokens_per_sec) << " tokens/sec" << std::endl;
        }

        std::cout << "[LLM_TRACE] Response length: " << response.length() << " chars" << std::endl;

        if (trace_config_.log_prompts || trace_config_.log_streaming) {
            std::cout << "[LLM_TRACE_RESPONSE] --- FULL RESPONSE ---" << std::endl;
            std::cout << response << std::endl;
            std::cout << "[LLM_TRACE_RESPONSE] --- END RESPONSE ---" << std::endl;
        }

        std::cout << "[LLM_TRACE] === REQUEST END ===" << std::endl;
    }

    return response;
}

// ============================================================================
// ReAct Iteration Helpers (for multi-step tool calling)
// ============================================================================

bool LLMSystem::has_tool_calls(const std::string& response) const {
    return response.find("<tool_call>") != std::string::npos;
}

LLMSystem::ToolCall LLMSystem::parse_first_tool_call(const std::string& response) const {
    size_t start = response.find("<tool_call>");
    if (start == std::string::npos) {
        return {};  // No tool call found
    }

    start += 11;  // Skip "<tool_call>"

    // Lenient parsing: Try to find closing tag, but also accept incomplete tool calls
    // This handles cases where LLM generates valid JSON but hits EOS before closing tag
    size_t end = response.find("</tool_call>", start);
    if (end == std::string::npos) {
        // No closing tag - try to extract JSON anyway (lenient mode)
        // Look for end of JSON object (last '}')
        size_t json_end = response.rfind('}', response.length());
        if (json_end != std::string::npos && json_end > start) {
            end = json_end + 1;  // Include the closing brace
        } else {
            return {};  // Can't find valid JSON structure
        }
    }

    std::string json_str = response.substr(start, end - start);

    // Trim whitespace
    json_str.erase(0, json_str.find_first_not_of(" \t\n\r"));
    json_str.erase(json_str.find_last_not_of(" \t\n\r") + 1);

    // Parse JSON (same logic as process_completed_responses)
    try {
        auto j = nlohmann::json::parse(json_str);
        ToolCall call;
        call.name = j.at("name").get<std::string>();
        // Store arguments as JSON string for execute_tool()
        call.args_json = j.at("arguments").dump();
        return call;
    } catch (const std::exception& e) {
        std::cerr << "[LLMSystem] Failed to parse tool call JSON: " << e.what() << std::endl;
        return {};  // Invalid JSON
    }
}

std::string LLMSystem::format_tool_response(const std::string& tool_name,
                                             const std::string& result) const {
    // Match Qwen 2.5 format for tool responses
    // <tool_response> with name and content
    std::ostringstream oss;
    oss << "<tool_response>\n";
    oss << "{\n";
    oss << "  \"name\": \"" << tool_name << "\",\n";
    oss << "  \"content\": " << result << "\n";  // result is already JSON
    oss << "}\n";
    oss << "</tool_response>";
    return oss.str();
}

} // namespace Logosphere
