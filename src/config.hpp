#pragma once

#include <string>
#include <cstdint>
#include "json.hpp"

struct Config {
    std::string ollama_url = "http://127.0.0.1:11434";
    std::string model = "";
    bool auto_approve_tools = false;
    bool allow_path_escape = false;
    bool markdown = false;
    size_t max_file_bytes = 262144;
    size_t max_tool_output_bytes = 65536;
    size_t max_history_messages = 200;
    // Compact automatically when estimated context reaches this percentage of
    // the context window (10-99). The window comes from Ollama /api/ps unless
    // context_limit overrides it.
    bool auto_compact = true;
    size_t compaction_threshold = 80;
    size_t context_limit = 0;  // 0 = detect from Ollama (/api/ps)
    std::string last_session;

    void load();
    void save() const;
    nlohmann::json to_json() const;
    void from_json(const nlohmann::json& j);
};
