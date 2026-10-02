#include "config.hpp"
#include "util.hpp"
#include <fstream>
#include <cstdlib>

void Config::load() {
    // Environment overrides URL
    if (const char* env = std::getenv("OLLAMA_HOST")) {
        if (*env) {
            std::string u = env;
            if (u.find("://") == std::string::npos) {
                u = "http://" + u;
            }
            ollama_url = u;
        }
    }
    if (const char* env = std::getenv("OLLAMA_URL")) {
        if (*env) ollama_url = env;
    }

    std::string path = util::config_dir() + "/config.json";
    std::ifstream in(path);
    if (!in) return;
    try {
        nlohmann::json j;
        in >> j;
        from_json(j);
    } catch (...) {
        // ignore malformed
    }
}

void Config::save() const {
    util::ensure_dir(util::config_dir());
    std::string path = util::config_dir() + "/config.json";
    std::ofstream out(path);
    if (!out) return;
    out << to_json().dump(2) << "\n";
}

nlohmann::json Config::to_json() const {
    return nlohmann::json{
        {"ollama_url", ollama_url},
        {"model", model},
        {"auto_approve_tools", auto_approve_tools},
        {"allow_path_escape", allow_path_escape},
        {"max_file_bytes", max_file_bytes},
        {"max_tool_output_bytes", max_tool_output_bytes},
        {"max_history_messages", max_history_messages},
        {"auto_compact", auto_compact},
        {"compaction_threshold", compaction_threshold},
        {"context_limit", context_limit},
        {"last_session", last_session}
    };
}

void Config::from_json(const nlohmann::json& j) {
    if (j.contains("ollama_url") && j["ollama_url"].is_string())
        ollama_url = j["ollama_url"].get<std::string>();
    if (j.contains("model") && j["model"].is_string())
        model = j["model"].get<std::string>();
    if (j.contains("auto_approve_tools") && j["auto_approve_tools"].is_boolean())
        auto_approve_tools = j["auto_approve_tools"].get<bool>();
    if (j.contains("allow_path_escape") && j["allow_path_escape"].is_boolean())
        allow_path_escape = j["allow_path_escape"].get<bool>();
    if (j.contains("max_file_bytes") && j["max_file_bytes"].is_number())
        max_file_bytes = j["max_file_bytes"].get<size_t>();
    if (j.contains("max_tool_output_bytes") && j["max_tool_output_bytes"].is_number())
        max_tool_output_bytes = j["max_tool_output_bytes"].get<size_t>();
    if (j.contains("max_history_messages") && j["max_history_messages"].is_number())
        max_history_messages = j["max_history_messages"].get<size_t>();
    if (j.contains("auto_compact") && j["auto_compact"].is_boolean())
        auto_compact = j["auto_compact"].get<bool>();
    if (j.contains("compaction_threshold") && j["compaction_threshold"].is_number())
        compaction_threshold = j["compaction_threshold"].get<size_t>();
    if (j.contains("context_limit") && j["context_limit"].is_number())
        context_limit = j["context_limit"].get<size_t>();
    if (compaction_threshold < 10) compaction_threshold = 10;
    if (compaction_threshold > 99) compaction_threshold = 99;
    if (j.contains("last_session") && j["last_session"].is_string())
        last_session = j["last_session"].get<std::string>();
}
