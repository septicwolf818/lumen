#include "ollama.hpp"
#include <curl/curl.h>

OllamaClient::OllamaClient(std::string base_url) : base_url_(std::move(base_url)) {
    while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

size_t OllamaClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct StreamCtx {
    std::string buffer;  // leftover incomplete lines
    std::function<void(const std::string&)> on_token;
    std::function<void(const std::string&)> on_thinking;
    std::function<void(const nlohmann::json&)> on_tool_call;
    std::function<bool()> should_abort;
    nlohmann::json* final_msg = nullptr;
    std::string content_acc;
    std::string thinking_acc;
    nlohmann::json tool_calls = nlohmann::json::array();
    bool aborted = false;
    std::string error;
    int prompt_eval = -1;
    int eval_count = -1;
};

size_t OllamaClient::stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamCtx*>(userdata);
    if (ctx->should_abort && ctx->should_abort()) {
        ctx->aborted = true;
        return 0;  // abort transfer
    }
    size_t total = size * nmemb;
    ctx->buffer.append(ptr, total);

    // Process complete lines (NDJSON)
    size_t pos = 0;
    for (;;) {
        size_t nl = ctx->buffer.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = ctx->buffer.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty() || line[0] == '\r') continue;
        // trim CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            auto j = nlohmann::json::parse(line);
            if (j.contains("error")) {
                ctx->error = j["error"].is_string()
                                 ? j["error"].get<std::string>()
                                 : j["error"].dump();
                continue;
            }
            if (j.contains("message") && j["message"].is_object()) {
                auto& msg = j["message"];
                if (msg.contains("content") && msg["content"].is_string()) {
                    std::string tok = msg["content"].get<std::string>();
                    if (!tok.empty()) {
                        ctx->content_acc += tok;
                        if (ctx->on_token) ctx->on_token(tok);
                    }
                }
                if (msg.contains("thinking") && msg["thinking"].is_string()) {
                    std::string tok = msg["thinking"].get<std::string>();
                    if (!tok.empty()) {
                        ctx->thinking_acc += tok;
                        if (ctx->on_thinking) ctx->on_thinking(tok);
                    }
                }
                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    // Ollama sends complete tool calls (several may share one
                    // chunk, without an index); OpenAI-style streams fragment
                    // them and tag each piece with an index. Fragments merge by
                    // index; a nameless piece continues the newest slot; a
                    // named piece without an index starts a new slot - so a
                    // multi-tool response is never fused into one corrupted
                    // entry with concatenated arguments.
                    for (auto& tc : msg["tool_calls"]) {
                        bool has_index =
                            tc.contains("index") && tc["index"].is_number();
                        bool named =
                            tc.contains("function") && tc["function"].is_object() &&
                            tc["function"].contains("name") &&
                            tc["function"]["name"].is_string() &&
                            !tc["function"]["name"].get<std::string>().empty();
                        size_t idx;
                        if (has_index) {
                            int64_t i = tc["index"].get<int64_t>();
                            // refuse absurd indexes - each one pre-allocates a slot
                            if (i < 0 || i > 63) continue;
                            idx = static_cast<size_t>(i);
                        } else if (named && !ctx->tool_calls.empty() &&
                                   ctx->tool_calls.back().contains("function") &&
                                   ctx->tool_calls.back()["function"].contains("name")) {
                            idx = ctx->tool_calls.size();  // next complete call
                        } else {
                            idx = ctx->tool_calls.empty() ? 0
                                                          : ctx->tool_calls.size() - 1;
                        }
                        while (ctx->tool_calls.size() <= idx)
                            ctx->tool_calls.push_back(nlohmann::json::object());
                        auto& dest = ctx->tool_calls[idx];
                        if (tc.contains("id")) dest["id"] = tc["id"];
                        if (tc.contains("type")) dest["type"] = tc["type"];
                        if (tc.contains("function") && tc["function"].is_object()) {
                            if (!dest.contains("function")) dest["function"] = nlohmann::json::object();
                            auto& fn = tc["function"];
                            if (fn.contains("name") && fn["name"].is_string() &&
                                !fn["name"].get<std::string>().empty())
                                dest["function"]["name"] = fn["name"];
                            if (fn.contains("arguments")) {
                                // arguments may arrive as incremental string
                                if (fn["arguments"].is_string()) {
                                    if (!dest["function"].contains("arguments"))
                                        dest["function"]["arguments"] = "";
                                    dest["function"]["arguments"] =
                                        dest["function"]["arguments"].get<std::string>()
                                        + fn["arguments"].get<std::string>();
                                } else {
                                    dest["function"]["arguments"] = fn["arguments"];
                                }
                            }
                        }
                    }
                }
            }
            if (j.value("done", false)) {
                if (j.contains("prompt_eval_count") && j["prompt_eval_count"].is_number())
                    ctx->prompt_eval = j["prompt_eval_count"].get<int>();
                if (j.contains("eval_count") && j["eval_count"].is_number())
                    ctx->eval_count = j["eval_count"].get<int>();
                // finalize
                if (ctx->final_msg) {
                    (*ctx->final_msg) = nlohmann::json{
                        {"role", "assistant"},
                        {"content", ctx->content_acc}
                    };
                    if (!ctx->thinking_acc.empty())
                        (*ctx->final_msg)["thinking"] = ctx->thinking_acc;
                    if (!ctx->tool_calls.empty()) {
                        // only include complete tool calls
                        nlohmann::json cleaned = nlohmann::json::array();
                        for (auto& tc : ctx->tool_calls) {
                            if (tc.contains("function") && tc["function"].is_object() &&
                                tc["function"].contains("name") &&
                                tc["function"]["name"].is_string() &&
                                !tc["function"]["name"].get<std::string>().empty()) {
                                cleaned.push_back(tc);
                            }
                        }
                        if (!cleaned.empty()) {
                            (*ctx->final_msg)["tool_calls"] = cleaned;
                            for (auto& tc : cleaned) {
                                if (ctx->on_tool_call) ctx->on_tool_call(tc);
                            }
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            // ignore parse errors on partial
        }
    }
    ctx->buffer.erase(0, pos);
    return total;
}

std::optional<std::vector<OllamaModel>> OllamaClient::list_models(std::string* err) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (err) *err = "curl init failed";
        return std::nullopt;
    }
    std::string url = base_url_ + "/api/tags";
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        if (err) *err = std::string("Cannot connect to Ollama at ") + base_url_ + ": " + curl_easy_strerror(rc);
        return std::nullopt;
    }
    if (http != 200) {
        if (err) *err = "Ollama /api/tags HTTP " + std::to_string(http);
        return std::nullopt;
    }
    try {
        auto j = nlohmann::json::parse(body);
        std::vector<OllamaModel> models;
        if (j.contains("models") && j["models"].is_array()) {
            for (auto& m : j["models"]) {
                OllamaModel om;
                if (m.contains("name") && m["name"].is_string())
                    om.name = m["name"].get<std::string>();
                models.push_back(om);
            }
        }
        return models;
    } catch (const std::exception& e) {
        if (err) *err = std::string("Bad JSON from Ollama: ") + e.what();
        return std::nullopt;
    }
}

int OllamaClient::context_length(const std::string& model) {
    if (model.empty()) return 0;
    CURL* curl = curl_easy_init();
    if (!curl) return 0;
    std::string url = base_url_ + "/api/ps";
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || http != 200) return 0;

    auto strip_tag = [](const std::string& s) {
        // Compare names before the ":tag" suffix ("llama3:8b" -> "llama3").
        size_t p = s.find(':');
        return p == std::string::npos ? s : s.substr(0, p);
    };
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("models") || !j["models"].is_array()) return 0;
        // exact match first, then match on the name without a tag
        for (int pass = 0; pass < 2; ++pass) {
            for (auto& m : j["models"]) {
                if (!m.contains("name") || !m["name"].is_string()) continue;
                std::string name = m["name"].get<std::string>();
                bool match = pass == 0
                                 ? (name == model)
                                 : (strip_tag(name) == strip_tag(model));
                if (!match) continue;
                if (m.contains("context_length") && m["context_length"].is_number()) {
                    int len = m["context_length"].get<int>();
                    if (len > 0) return len;
                }
            }
        }
    } catch (...) {
    }
    return 0;
}

bool OllamaClient::chat_stream(
    const std::string& model,
    const std::vector<nlohmann::json>& messages,
    const nlohmann::json& tools,
    std::function<void(const std::string& token)> on_token,
    std::function<void(const std::string& token)> on_thinking,
    std::function<void(const nlohmann::json& tool_call)> on_tool_call,
    std::function<bool()> should_abort,
    std::string* err,
    nlohmann::json* final_message,
    OllamaUsage* usage
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (err) *err = "curl init failed";
        return false;
    }

    nlohmann::json req;
    req["model"] = model;
    req["messages"] = messages;
    req["stream"] = true;
    if (!tools.is_null() && tools.is_array() && !tools.empty()) {
        req["tools"] = tools;
    }

    std::string payload = req.dump();
    std::string url = base_url_ + "/api/chat";

    StreamCtx ctx;
    ctx.on_token = std::move(on_token);
    ctx.on_thinking = std::move(on_thinking);
    ctx.on_tool_call = std::move(on_tool_call);
    ctx.should_abort = std::move(should_abort);
    ctx.final_msg = final_message;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);  // no total timeout for long gens
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    // Drive the transfer with the multi interface: the wait below wakes us up
    // every 50 ms so should_abort (and the terminal input pump) keeps running
    // even while the model is silent between tokens.
    CURLM* multi = curl_multi_init();
    if (!multi) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (err) *err = "curl multi init failed";
        return false;
    }
    curl_multi_add_handle(multi, curl);

    CURLcode rc = CURLE_OK;
    CURLMcode mc = CURLM_OK;
    int still_running = 1;
    mc = curl_multi_perform(multi, &still_running);
    while (still_running > 0 && mc == CURLM_OK) {
        if (ctx.should_abort && ctx.should_abort()) {
            ctx.aborted = true;
            break;
        }
        int numfds = 0;
        mc = curl_multi_wait(multi, nullptr, 0, 50, &numfds);
        if (mc != CURLM_OK) break;
        mc = curl_multi_perform(multi, &still_running);
    }

    int msgs_left = 0;
    while (CURLMsg* msg = curl_multi_info_read(multi, &msgs_left)) {
        if (msg->msg == CURLMSG_DONE && msg->easy_handle == curl) rc = msg->data.result;
    }
    if (mc != CURLM_OK && rc == CURLE_OK && !ctx.aborted) {
        ctx.error = std::string("curl multi failed: ") + curl_multi_strerror(mc);
    }

    curl_multi_remove_handle(multi, curl);
    curl_multi_cleanup(multi);

    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (usage) {
        usage->prompt_eval = ctx.prompt_eval;
        usage->eval = ctx.eval_count;
    }

    if (ctx.aborted) {
        if (err) *err = "interrupted";
        // still return whatever we have
        if (final_message && final_message->empty()) {
            *final_message = nlohmann::json{
                {"role", "assistant"},
                {"content", ctx.content_acc}
            };
            if (!ctx.thinking_acc.empty())
                (*final_message)["thinking"] = ctx.thinking_acc;
        }
        return true;  // partial success
    }
    if (rc != CURLE_OK && rc != CURLE_WRITE_ERROR) {
        if (err) *err = std::string("Ollama request failed: ") + curl_easy_strerror(rc);
        return false;
    }
    if (!ctx.error.empty()) {
        if (err) *err = ctx.error;
        return false;
    }
    if (http != 200 && http != 0) {
        if (err) *err = "Ollama HTTP " + std::to_string(http);
        return false;
    }
    return true;
}
