#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace util {

std::string home_dir();
std::string config_dir();
std::string data_dir();
std::string sessions_dir();

bool ensure_dir(const std::string& path);

std::string now_iso8601();
std::string trim(const std::string& s);
std::vector<std::string> split_lines(const std::string& s);
std::string join(const std::vector<std::string>& parts, const std::string& sep);

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

// https://no-color.org - true when NO_COLOR is present and non-empty, meaning
// output must be emitted without any ANSI colour/attribute sequences.
bool no_color();

uint64_t steady_ms();

// UTF-8 / binary safety (nlohmann::json requires valid UTF-8 strings)
bool is_valid_utf8(const std::string& s);
// Replace invalid sequences with U+FFFD; also strips NULs
std::string sanitize_utf8(const std::string& s);
// Heuristic: NUL bytes or mostly non-text → binary
bool looks_binary(const std::string& s, size_t sample = 8192);

} // namespace util
