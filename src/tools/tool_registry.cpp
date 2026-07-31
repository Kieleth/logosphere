#include "tool_registry.h"
#include <iostream>
#include <sstream>

ToolRegistry::ToolRegistry() {
}

ToolRegistry::~ToolRegistry() {
}

void ToolRegistry::register_tool(const Tool& tool) {
    tools_[tool.name] = tool;
    std::cout << "[ToolRegistry] Registered tool: " << tool.name << std::endl;
}

std::vector<Tool> ToolRegistry::get_all_tools() const {
    std::vector<Tool> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }
    return result;
}

bool ToolRegistry::has_tool(const std::string& tool_name) const {
    return tools_.count(tool_name) > 0;
}

std::string ToolRegistry::execute_tool(const std::string& tool_name,
                                       const nlohmann::json& args) {
    try {
        // Check if tool exists
        if (!has_tool(tool_name)) {
            return "ERROR: Unknown tool '" + tool_name + "'";
        }

        const Tool& tool = tools_.at(tool_name);

        // Validate required parameters
        std::string validation_error;
        if (!validate_parameters(tool, args, validation_error)) {
            return "ERROR: " + validation_error;
        }

        // Execute tool
        std::cout << "[ToolRegistry] Executing: " << tool_name << std::endl;
        return tool.execute(args);

    } catch (const std::exception& e) {
        return "ERROR: Tool execution failed: " + std::string(e.what());
    }
}

bool ToolRegistry::validate_parameters(const Tool& tool,
                                       const nlohmann::json& args,
                                       std::string& error) {
    // Check all required parameters are present
    for (const auto& param : tool.parameters) {
        if (param.required && !args.contains(param.name)) {
            error = "Missing required parameter '" + param.name + "' for tool '" + tool.name + "'";
            return false;
        }
    }
    return true;
}

std::string ToolRegistry::to_prompt_format() const {
    std::cout << "[ToolRegistry] to_prompt_format() called, tools_.size()=" << tools_.size() << std::endl;

    std::ostringstream oss;

    // Hermes-style JSON function definitions (Qwen2.5 compatible)
    oss << "You have access to the following functions:\n[\n";

    bool first_tool = true;
    for (const auto& [name, tool] : tools_) {
        std::cout << "[ToolRegistry] Processing tool: " << name << std::endl;

        if (!first_tool) oss << ",\n";
        first_tool = false;

        oss << "  {\"type\": \"function\", \"function\": {\n";
        oss << "    \"name\": \"" << tool.name << "\",\n";
        oss << "    \"description\": \"" << tool.description << "\",\n";
        oss << "    \"parameters\": {\"type\": \"object\", \"properties\": {\n";

        // Parameters as JSON schema
        bool first_param = true;
        for (const auto& param : tool.parameters) {
            if (!first_param) oss << ",\n";
            first_param = false;

            oss << "      \"" << param.name << "\": {\n";
            oss << "        \"type\": \"" << param.type << "\",\n";
            oss << "        \"description\": \"" << param.description << "\"";
            if (!param.default_value.empty()) {
                oss << ",\n        \"default\": \"" << param.default_value << "\"";
            }
            oss << "\n      }";
        }

        oss << "\n    }, \"required\": [";

        // Required parameters list
        bool first_req = true;
        for (const auto& param : tool.parameters) {
            if (param.required) {
                if (!first_req) oss << ", ";
                oss << "\"" << param.name << "\"";
                first_req = false;
            }
        }

        oss << "]}\n";
        oss << "  }}";
    }

    oss << "\n]\n\n";

    // Hermes-style tool call format
    oss << "To call a function, respond with:\n";
    oss << "<tool_call>\n";
    oss << "{\"name\": \"function_name\", \"arguments\": {\"param1\": value1, \"param2\": value2}}\n";
    oss << "</tool_call>\n";

    std::cout << "[ToolRegistry] to_prompt_format() completed successfully" << std::endl;
    return oss.str();
}
