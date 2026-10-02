#include "session.hpp"
#include "util.hpp"
#include <fstream>
#include <random>
#include <sstream>
#include <algorithm>

static std::string role_to_str(Role r) {
    switch (r) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "user";
}

static Role str_to_role(const std::string& s) {
    if (s == "system") return Role::System;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool") return Role::Tool;
    return Role::User;
}

nlohmann::json Message::to_json() const {
    nlohmann::json j;
    j["role"] = role_to_str(role);
    // nlohmann::json throws type_error.316 on invalid UTF-8 — always sanitize
    j["content"] = util::sanitize_utf8(content);
    if (!name.empty()) j["name"] = util::sanitize_utf8(name);
    if (!tool_call_id.empty()) j["tool_call_id"] = util::sanitize_utf8(tool_call_id);
    if (!tool_calls.is_null() && !tool_calls.empty()) j["tool_calls"] = tool_calls;
    return j;
}

Message Message::from_json(const nlohmann::json& j) {
    Message m;
    if (j.contains("role")) m.role = str_to_role(j["role"].get<std::string>());
    if (j.contains("content") && j["content"].is_string())
        m.content = j["content"].get<std::string>();
    else if (j.contains("content") && j["content"].is_null())
        m.content = "";
    if (j.contains("name") && j["name"].is_string())
        m.name = j["name"].get<std::string>();
    if (j.contains("tool_call_id") && j["tool_call_id"].is_string())
        m.tool_call_id = j["tool_call_id"].get<std::string>();
    if (j.contains("tool_calls")) m.tool_calls = j["tool_calls"];
    return m;
}

nlohmann::json Session::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["model"] = model;
    j["created"] = created;
    j["modified"] = modified;
    j["cwd"] = cwd;
    j["messages"] = nlohmann::json::array();
    for (const auto& m : messages) {
        j["messages"].push_back(m.to_json());
    }
    return j;
}

Session Session::from_json(const nlohmann::json& j) {
    Session s;
    if (j.contains("id")) s.id = j["id"].get<std::string>();
    if (j.contains("name")) s.name = j["name"].get<std::string>();
    if (j.contains("model")) s.model = j["model"].get<std::string>();
    if (j.contains("created")) s.created = j["created"].get<std::string>();
    if (j.contains("modified")) s.modified = j["modified"].get<std::string>();
    if (j.contains("cwd")) s.cwd = j["cwd"].get<std::string>();
    if (j.contains("messages") && j["messages"].is_array()) {
        for (const auto& mj : j["messages"]) {
            s.messages.push_back(Message::from_json(mj));
        }
    }
    return s;
}

std::string Session::make_id() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string id = "s-";
    for (int i = 0; i < 12; ++i) id += hex[dist(rng)];
    return id;
}

bool Session::save() const {
    util::ensure_dir(util::sessions_dir());
    // Use name as filename if safe, else id
    std::string fname = name.empty() ? id : name;
    // sanitize
    for (char& c : fname) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            c = '_';
    }
    if (fname.empty()) fname = id;
    std::string path = util::sessions_dir() + "/" + fname + ".json";
    std::ofstream out(path);
    if (!out) return false;
    out << to_json().dump(2) << "\n";
    return true;
}

std::optional<Session> Session::load(const std::string& name_or_id) {
    util::ensure_dir(util::sessions_dir());
    // try exact filename stem
    std::string path = util::sessions_dir() + "/" + name_or_id + ".json";
    {
        std::ifstream in(path);
        if (in) {
            try {
                nlohmann::json j;
                in >> j;
                return from_json(j);
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    // fallback: scan all sessions for matching id or name
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(util::sessions_dir(), ec)) {
        if (!ent.is_regular_file()) continue;
        auto p = ent.path();
        if (p.extension() != ".json") continue;
        std::ifstream f(p);
        if (!f) continue;
        try {
            nlohmann::json j;
            f >> j;
            Session s = from_json(j);
            if (s.id == name_or_id || s.name == name_or_id || p.stem() == name_or_id) {
                return s;
            }
        } catch (...) {}
    }
    return std::nullopt;
}

std::vector<std::string> Session::list() {
    std::vector<std::string> names;
    util::ensure_dir(util::sessions_dir());
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(util::sessions_dir(), ec)) {
        if (!ent.is_regular_file()) continue;
        auto p = ent.path();
        if (p.extension() == ".json") {
            names.push_back(p.stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Session::remove(const std::string& name_or_id) {
    std::string path = util::sessions_dir() + "/" + name_or_id + ".json";
    std::error_code ec;
    if (fs::remove(path, ec)) return true;
    // scan
    for (auto& ent : fs::directory_iterator(util::sessions_dir(), ec)) {
        if (!ent.is_regular_file()) continue;
        auto p = ent.path();
        if (p.extension() != ".json") continue;
        std::ifstream f(p);
        if (!f) continue;
        try {
            nlohmann::json j;
            f >> j;
            Session s = from_json(j);
            if (s.id == name_or_id || s.name == name_or_id) {
                return fs::remove(p, ec);
            }
        } catch (...) {}
    }
    return false;
}
