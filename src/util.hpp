#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>
#include <filesystem>
#include "json.hpp"

namespace fs = std::filesystem;

namespace util {

std::string home_dir();
std::string config_dir();
std::string data_dir();
std::string sessions_dir();

bool ensure_dir(const std::string& path);

// Write via temp file + rename so a crash cannot leave a truncated file.
bool write_file_atomic(const std::string& path, const std::string& data);

std::string now_iso8601();
std::string trim(const std::string& s);
std::vector<std::string> split_lines(const std::string& s);

// Expand a leading "~"/"~/" to home; unresolvable forms are returned as-is.
std::string expand_path(const std::string& p);

// Safe path: resolve relative to cwd and optionally forbid escaping cwd
std::optional<fs::path> resolve_safe_path(const fs::path& cwd,
                                          const std::string& user_path,
                                          bool allow_escape);

// Truncate string with notice
std::string truncate(const std::string& s, size_t max_bytes, const std::string& notice = "\n[output truncated]\n");

// Compact token count for status display: 812, 12.4k, 1.2M
std::string fmt_tokens(size_t n);

// Simple shell-escape for display
std::string shell_escape(const std::string& s);

// https://no-color.org - true when NO_COLOR is set non-empty (emit no ANSI).
bool no_color();

uint64_t steady_ms();

// UTF-8 / binary safety (nlohmann::json requires valid UTF-8 strings)
bool is_valid_utf8(const std::string& s);
// Replace invalid sequences with U+FFFD; also strips NULs
std::string sanitize_utf8(const std::string& s);
// Recursively sanitize every string in a JSON value, so dump() never throws
// type_error.316 on invalid UTF-8 arriving from the model or tool output.
void sanitize_json(nlohmann::json& j);
// Heuristic: NUL bytes or mostly non-text → binary
bool looks_binary(const std::string& s, size_t sample = 8192);

}  // namespace util
