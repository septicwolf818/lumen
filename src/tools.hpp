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

    // Interactive handler for the ask tool: shows question + options and
    // returns the user's answer, or nullopt when cancelled. Set by the
    // Agent (which owns the UI); without it ask calls fail cleanly.
    using AskFn = std::function<std::optional<std::string>(
        const std::string& question, const std::vector<std::string>& options)>;
    void set_ask_fn(AskFn fn) { ask_fn_ = std::move(fn); }

    // True if the tool is read-only (no confirmation needed).
    static bool is_readonly(const std::string& name);

    // Run a tool call. confirm_fn is called when needed; false cancels it.
    // pump_fn is called while a tool that waits on a child process runs, so the
    // UI keeps refreshing and typing keeps working; returning true aborts it.
    ToolResult run(const std::string& name,
                   const nlohmann::json& arguments,
                   std::function<bool(const std::string& prompt)> confirm_fn,
                   std::function<bool()> pump_fn = {});

private:
    Config& cfg_;
    std::string cwd_;
    AskFn ask_fn_;

    ToolResult ask(const nlohmann::json& args);

    ToolResult read_file(const nlohmann::json& args);
    ToolResult write_file(const nlohmann::json& args);
    ToolResult edit_file(const nlohmann::json& args);
    ToolResult list_directory(const nlohmann::json& args);
    ToolResult execute(const nlohmann::json& args, const std::function<bool()>& pump);
    ToolResult search(const nlohmann::json& args, const std::function<bool()>& pump);

    std::optional<std::string> safe_path(const std::string& user_path);
    // Error text for a path that resolve_safe_path rejected
    std::string path_denied(const std::string& user_path) const;
};
