#include "config.hpp"
#include "util.hpp"
#include <fstream>
#include <cstdlib>

void Config::load() {
    std::string path = util::config_dir() + "/config.json";
    std::ifstream in(path);
    if (in) {
        try {
            nlohmann::json j;
            in >> j;
            from_json(j);
        } catch (...) {
            // ignore malformed
        }
    }

    // Env overrides the file value, so it must run after from_json().
    auto env_url = [this](const char* name) {
        const char* env = std::getenv(name);
        if (!env || !*env) return;
        std::string u = env;
        if (u.find("://") == std::string::npos) u = "http://" + u;
        ollama_url = u;
    };
    env_url("OLLAMA_HOST");
    env_url("OLLAMA_URL");
}

void Config::save() const {
    util::ensure_dir(util::config_dir());
    std::string path = util::config_dir() + "/config.json";
    util::write_file_atomic(path, to_json().dump(2) + "\n");
}

nlohmann::json Config::to_json() const {
    return nlohmann::json{
        {"ollama_url", ollama_url},
        {"model", model},
        {"auto_approve_tools", auto_approve_tools},
        {"allow_path_escape", allow_path_escape},
        {"markdown", markdown},
        {"max_file_bytes", max_file_bytes},
        {"max_tool_output_bytes", max_tool_output_bytes},
        {"command_timeout", command_timeout},
        {"max_history_messages", max_history_messages},
        {"auto_compact", auto_compact},
        {"compaction_threshold", compaction_threshold},
        {"context_limit", context_limit},
        {"last_session", last_session}
    };
}

void Config::from_json(const nlohmann::json& j) {
    // Clamp negatives, which would wrap to huge size_t values.
    auto usize = [](const nlohmann::json& v, size_t def) -> size_t {
        if (!v.is_number_integer()) return def;
        int64_t n = v.get<int64_t>();
        return n < 0 ? def : static_cast<size_t>(n);
    };
    if (j.contains("ollama_url") && j["ollama_url"].is_string())
        ollama_url = j["ollama_url"].get<std::string>();
    if (j.contains("model") && j["model"].is_string())
        model = j["model"].get<std::string>();
    if (j.contains("auto_approve_tools") && j["auto_approve_tools"].is_boolean())
        auto_approve_tools = j["auto_approve_tools"].get<bool>();
    if (j.contains("allow_path_escape") && j["allow_path_escape"].is_boolean())
        allow_path_escape = j["allow_path_escape"].get<bool>();
    if (j.contains("markdown") && j["markdown"].is_boolean())
        markdown = j["markdown"].get<bool>();
    if (j.contains("max_file_bytes"))
        max_file_bytes = usize(j["max_file_bytes"], max_file_bytes);
    if (j.contains("max_tool_output_bytes"))
        max_tool_output_bytes = usize(j["max_tool_output_bytes"], max_tool_output_bytes);
    if (j.contains("command_timeout"))
        command_timeout = usize(j["command_timeout"], command_timeout);
    if (j.contains("max_history_messages"))
        max_history_messages = usize(j["max_history_messages"], max_history_messages);
    if (j.contains("auto_compact") && j["auto_compact"].is_boolean())
        auto_compact = j["auto_compact"].get<bool>();
    if (j.contains("compaction_threshold"))
        compaction_threshold = usize(j["compaction_threshold"], compaction_threshold);
    if (j.contains("context_limit"))
        context_limit = usize(j["context_limit"], context_limit);
    if (compaction_threshold < 10) compaction_threshold = 10;
    if (compaction_threshold > 99) compaction_threshold = 99;
    if (j.contains("last_session") && j["last_session"].is_string())
        last_session = j["last_session"].get<std::string>();
}
