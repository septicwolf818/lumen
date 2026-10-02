#pragma once

#include <string>
#include <vector>
#include <optional>
#include "json.hpp"

enum class Role { System, User, Assistant, Tool };

struct Message {
    Role role = Role::User;
    std::string content;
    std::string thinking;         // reasoning of thinking models (display only)
    std::string name;           // for tool results
    std::string tool_call_id;   // optional
    // For assistant messages that contain tool calls
    nlohmann::json tool_calls;  // array or null

    nlohmann::json to_json() const;
    static Message from_json(const nlohmann::json& j);
};

struct Session {
    std::string id;
    std::string name;
    std::string model;
    std::string created;
    std::string modified;
    std::string cwd;
    std::vector<Message> messages;

    nlohmann::json to_json() const;
    static Session from_json(const nlohmann::json& j);

    bool save() const;
    static std::optional<Session> load(const std::string& name_or_id);
    static std::vector<std::string> list();
    static bool remove(const std::string& name_or_id);
    static std::string make_id();
    // Filesystem-safe form of a session name (shared by save and rename)
    static std::string sanitize_filename(const std::string& name);
};
