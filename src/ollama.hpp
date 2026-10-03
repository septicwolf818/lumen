#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "json.hpp"
#include "session.hpp"

struct OllamaModel {
    std::string name;
};

// Token counts reported by Ollama for the last request (-1 = not reported).
struct OllamaUsage {
    int prompt_eval = -1;  // input tokens actually fed to the model
    int eval = -1;         // output tokens generated
};

class OllamaClient {
public:
    explicit OllamaClient(std::string base_url);

    void set_base_url(const std::string& url) { base_url_ = url; }
    const std::string& base_url() const { return base_url_; }

    // GET /api/tags
    std::optional<std::vector<OllamaModel>> list_models(std::string* err = nullptr);

    // Streaming chat. on_token/on_thinking fire per content/reasoning delta
    // (plain models never call on_thinking); on_tool_call fires per completed
    // tool call. Messages are in Ollama format. Returns true on success (a
    // requested abort is still a success). usage, if given, receives the
    // token counts from the final chunk.
    bool chat_stream(
        const std::string& model,
        const std::vector<nlohmann::json>& messages,
        const nlohmann::json& tools,  // array of tool defs, or null
        std::function<void(const std::string& token)> on_token,
        std::function<void(const std::string& token)> on_thinking,
        std::function<void(const nlohmann::json& tool_call)> on_tool_call,
        std::function<bool()> should_abort,  // return true to abort
        std::string* err = nullptr,
        nlohmann::json* final_message = nullptr,  // filled with full assistant message
        OllamaUsage* usage = nullptr
    );

    // Allocated context length of the running model (GET /api/ps); 0 if unknown.
    int context_length(const std::string& model);

private:
    std::string base_url_;
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
};
