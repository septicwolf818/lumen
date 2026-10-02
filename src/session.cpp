#include "session.hpp"
#include "util.hpp"
#include <fstream>
#include <random>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

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

// A flat, single-component file stem. Names that could climb out of the
// sessions directory ("/", "..", separators) are skipped for direct path
// use; callers fall back to scanning file contents instead.
static bool flat_stem(const std::string& s) {
    if (s.empty() || s == "." || s == "..") return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            return false;
    }
    return true;
}

nlohmann::json Message::to_json() const {
    nlohmann::json j;
    j["role"] = role_to_str(role);
    // nlohmann::json throws type_error.316 on invalid UTF-8 — always sanitize
    j["content"] = util::sanitize_utf8(content);
    if (!thinking.empty()) j["thinking"] = util::sanitize_utf8(thinking);
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
    if (j.contains("thinking") && j["thinking"].is_string())
        m.thinking = j["thinking"].get<std::string>();
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

std::string Session::sanitize_filename(const std::string& name) {
    std::string fname = name;
    for (char& c : fname) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            c = '_';
    }
    return fname;
}

std::string Session::slugify(const std::string& raw) {
    std::string out;
    bool prev_dash = false;
    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            prev_dash = false;
        } else if (!out.empty() && !prev_dash) {
            out += '-';  // spaces, punctuation, quotes, markdown, ...
            prev_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 60) {
        out.resize(60);
        size_t cut = out.find_last_of('-');
        if (cut != std::string::npos && cut >= 12) out.resize(cut);
        while (!out.empty() && out.back() == '-') out.pop_back();
    }
    return out;
}

std::string Session::unique_title(const std::string& base,
                                  const std::string& exclude_stem) {
    std::string b = base.empty() ? "session" : base;
    // Session files are addressed by filename stem (save uses the sanitized
    // name, or the id), so stems are exactly what must not collide - two
    // sessions sharing a stem would overwrite each other's file.
    auto taken = [&exclude_stem](const std::string& cand) {
        for (const auto& s : Session::list()) {
            if (s == cand && s != exclude_stem) return true;
        }
        return false;
    };
    if (!taken(b)) return b;
    for (int i = 2; i < 1000; ++i) {
        std::string c = b + "-" + std::to_string(i);
        if (!taken(c)) return c;
    }
    return b + "-" + make_id().substr(2, 6);  // pathological, never in practice
}

bool Session::save() const {
    util::ensure_dir(util::sessions_dir());
    // Use name as filename if safe, else id
    std::string fname = name.empty() ? id : sanitize_filename(name);
    if (fname.empty()) fname = id;
    std::string path = util::sessions_dir() + "/" + fname + ".json";
    // Write to a temporary file first: a crash mid-write must not destroy
    // the existing session (a truncated file would fail to parse on load).
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) return false;
        out << to_json().dump(2) << "\n";
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
    return true;
}

std::optional<Session> Session::load(const std::string& name_or_id) {
    util::ensure_dir(util::sessions_dir());
    // try exact filename stem
    if (flat_stem(name_or_id)) {
        std::string path = util::sessions_dir() + "/" + name_or_id + ".json";
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
        std::error_code tec;
        if (!ent.is_regular_file(tec)) continue;
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
        std::error_code tec;
        if (!ent.is_regular_file(tec)) continue;
        auto p = ent.path();
        if (p.extension() == ".json") {
            names.push_back(p.stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<Session::Info> Session::list_info() {
    std::vector<Info> out;
    util::ensure_dir(util::sessions_dir());
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(util::sessions_dir(), ec)) {
        std::error_code tec;
        if (!ent.is_regular_file(tec)) continue;
        auto p = ent.path();
        if (p.extension() != ".json") continue;

        Info info;
        info.name = p.stem().string();

        // The file mtime is the last save, i.e. the last activity, and needs
        // no parsing of (possibly large) session JSON.
        std::error_code fec;
        auto ftime = fs::last_write_time(p, fec);
        if (!fec) {
            // Portable file_clock -> system_clock conversion (C++17).
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t t = std::chrono::system_clock::to_time_t(sctp);
            std::tm tmv{};
            if (localtime_r(&t, &tmv)) {
                char buf[32];
                if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv))
                    info.modified = buf;
            }
        }
        out.push_back(std::move(info));
    }
    // Most recent first; names break ties and order files with no timestamp.
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        return a.name < b.name;
    });
    return out;
}

bool Session::remove(const std::string& name_or_id) {
    if (flat_stem(name_or_id)) {
        std::string path = util::sessions_dir() + "/" + name_or_id + ".json";
        std::error_code ec;
        if (fs::remove(path, ec)) return true;
    }
    // scan
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(util::sessions_dir(), ec)) {
        std::error_code tec;
        if (!ent.is_regular_file(tec)) continue;
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
