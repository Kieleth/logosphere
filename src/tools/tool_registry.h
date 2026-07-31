#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#ifdef HAS_LLAMA

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

// Tool parameter metadata
struct ToolParameter {
    std::string name;           // "x", "y", "spec"
    std::string type;           // "number", "string", "boolean"
    std::string description;    // "X coordinate in world space"
    bool required;              // true/false
    std::string default_value;  // "0.0", "monarch"
};

// Tool definition with execution function
struct Tool {
    std::string name;           // "create_butterfly"
    std::string description;    // "Create a butterfly at specified position"
    std::vector<ToolParameter> parameters;

    // C++ function to execute
    using ToolFunction = std::function<std::string(const nlohmann::json& args)>;
    ToolFunction execute;
};

// Central registry of all available tools
class ToolRegistry {
public:
    ToolRegistry();
    ~ToolRegistry();

    // Register a tool
    void register_tool(const Tool& tool);

    // Get all tools (for LLM discovery)
    std::vector<Tool> get_all_tools() const;

    // Execute tool by name with arguments
    // Returns: Result message or error string
    std::string execute_tool(const std::string& tool_name,
                            const nlohmann::json& args);

    // Check if tool exists
    bool has_tool(const std::string& tool_name) const;

    // Generate tool descriptions for system prompt (plain text format)
    std::string to_prompt_format() const;

private:
    std::unordered_map<std::string, Tool> tools_;

    // Validate required parameters
    bool validate_parameters(const Tool& tool, const nlohmann::json& args, std::string& error);
};

#endif // HAS_LLAMA

#endif // TOOL_REGISTRY_H
