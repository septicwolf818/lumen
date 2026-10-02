#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "json.hpp"
#include "session.hpp"

struct OllamaModel {
    std::string name;
    std::string model;  // same as name usually
    int64_t size = 0;
    std::string modified;
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

    // Streaming chat. on_token called for each content delta.
    // on_thinking called for each reasoning delta (thinking models only;
    // plain models simply never call it).
    // on_tool_call called when a tool call is complete (name, args_json, id).
    // Returns true on success (may be interrupted).
    // messages are in Ollama format.
    // usage, if given, receives the token counts from the done chunk.
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

    // Non-streaming simple generate (for /compact)
    std::optional<std::string> generate(
        const std::string& model,
        const std::string& prompt,
        std::string* err = nullptr
    );

    // Allocated context length of the running model (GET /api/ps
    // context_length). 0 if unknown (model not loaded / request failed).
    int context_length(const std::string& model);

private:
    std::string base_url_;
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
};
