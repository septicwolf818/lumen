#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "json.hpp"
#include "config.hpp"

struct ToolResult {
    bool ok = true;
    std::string output;
};

// Tool definitions for Ollama
nlohmann::json tool_definitions();

class ToolRunner {
public:
    ToolRunner(Config& cfg, std::string cwd);

    void set_cwd(const std::string& cwd) { cwd_ = cwd; }
    const std::string& cwd() const { return cwd_; }

    // True if the tool is read-only (no confirmation needed).
    static bool is_readonly(const std::string& name);

    // Run a tool call. confirm_fn is called when needed; false cancels it.
    ToolResult run(const std::string& name,
                   const nlohmann::json& arguments,
                   std::function<bool(const std::string& prompt)> confirm_fn);

private:
    Config& cfg_;
    std::string cwd_;

    ToolResult read_file(const nlohmann::json& args);
    ToolResult write_file(const nlohmann::json& args);
    ToolResult edit_file(const nlohmann::json& args);
    ToolResult list_directory(const nlohmann::json& args);
    ToolResult execute(const nlohmann::json& args);
    ToolResult search(const nlohmann::json& args);

    std::optional<std::string> safe_path(const std::string& user_path);
    // Error text for a path that resolve_safe_path rejected
    std::string path_denied(const std::string& user_path) const;
};
