#include "util.hpp"
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <pwd.h>
#include <cwchar>
#include <fcntl.h>
#include <sys/stat.h>

namespace util {

std::string home_dir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return ".";
}

std::string config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/lumen";
    return home_dir() + "/.config/lumen";
}

std::string data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/lumen";
    return home_dir() + "/.local/share/lumen";
}

std::string sessions_dir() {
    return data_dir() + "/sessions";
}

bool ensure_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

bool write_file_atomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp.XXXXXX";
    std::vector<char> name(tmp.begin(), tmp.end());
    name.push_back('\0');
    int fd = ::mkstemp(name.data());
    if (fd < 0) return false;
    tmp.assign(name.data());

    struct stat st{};
    mode_t mode = 0;
    if (::stat(path.c_str(), &st) == 0) {
        mode = st.st_mode & 07777;
    } else {
        mode_t mask = ::umask(0);
        ::umask(mask);
        mode = static_cast<mode_t>(0666 & ~mask);
    }

    bool ok = ::fchmod(fd, mode) == 0;
    size_t off = 0;
    while (ok && off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        off += static_cast<size_t>(n);
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;

    if (ok && ::rename(tmp.c_str(), path.c_str()) == 0) return true;
    ::unlink(tmp.c_str());
    return false;
}

std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        out.push_back(line);
    }
    return out;
}

std::string expand_path(const std::string& p) {
    if (p == "~") return home_dir();
    if (p.rfind("~/", 0) == 0) return home_dir() + p.substr(1);
    return p;
}

std::optional<fs::path> resolve_safe_path(const fs::path& cwd,
                                          const std::string& user_path,
                                          bool allow_escape) {
    try {
        fs::path p = expand_path(user_path);  // "~/x" must not become cwd/"~/x"
        if (p.is_relative()) {
            p = cwd / p;
        }
        p = fs::weakly_canonical(p);
        if (!allow_escape) {
            fs::path base = fs::weakly_canonical(cwd);
            auto pstr = p.string();
            auto bstr = base.string();
            // Must be base itself or base + '/...': a bare prefix match would
            // admit a sibling like "/work/app-evil" under base "/work/app".
            bool under = pstr.size() >= bstr.size() &&
                         pstr.compare(0, bstr.size(), bstr) == 0;
            if (under && pstr.size() > bstr.size() && bstr.back() != '/' &&
                pstr[bstr.size()] != '/') {
                under = false;  // sibling directory sharing the prefix
            }
            if (!under) return std::nullopt;
        }
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

std::string truncate(const std::string& s, size_t max_bytes, const std::string& notice) {
    if (s.size() <= max_bytes) return s;
    size_t keep = max_bytes > notice.size() ? max_bytes - notice.size() : max_bytes / 2;
    return s.substr(0, keep) + notice;
}

std::string fmt_tokens(size_t n) {
    if (n < 1000) return std::to_string(n);
    if (n < 1000000) {
        size_t whole = n / 1000;
        size_t tenth = (n % 1000) / 100;
        if (tenth == 0) return std::to_string(whole) + "k";
        return std::to_string(whole) + "." + std::to_string(tenth) + "k";
    }
    if (n < 1000000000) {
        size_t whole = n / 1000000;
        size_t tenth = (n % 1000000) / 100000;
        if (tenth == 0) return std::to_string(whole) + "M";
        return std::to_string(whole) + "." + std::to_string(tenth) + "M";
    }
    return std::to_string(n / 1000000000) + "B";
}

std::string shell_escape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

bool no_color() {
    static const bool set = [] {  // read once: NO_COLOR never changes at runtime
        const char* v = std::getenv("NO_COLOR");
        return v != nullptr && *v != '\0';
    }();
    return set;
}

uint64_t steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {

bool decode_utf8(const unsigned char* p, size_t remain, uint32_t& cp, size_t& len) {
    if (remain == 0) return false;
    unsigned char c = p[0];
    if (c <= 0x7F) { cp = c; len = 1; return true; }
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        if (remain < len || (p[1] & 0xC0) != 0x80) return false;
        cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        return true;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        if (remain < len || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
            (c == 0xE0 && p[1] < 0xA0) || (c == 0xED && p[1] >= 0xA0)) return false;
        cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return true;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        if (remain < len || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
            (p[3] & 0xC0) != 0x80 || (c == 0xF0 && p[1] < 0x90) ||
            (c == 0xF4 && p[1] > 0x8F)) return false;
        cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
             ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return true;
    }
    return false;
}

int codepoint_width(uint32_t cp) {
    if (cp > static_cast<uint32_t>(WCHAR_MAX)) return 1;
    int w = ::wcwidth(static_cast<wchar_t>(cp));
    return w < 0 ? 1 : w;
}

}  // namespace

bool is_valid_utf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        size_t len = 0;
        if (!decode_utf8(p + i, s.size() - i, cp, len)) return false;
        i += len;
    }
    return true;
}

std::string sanitize_utf8(const std::string& s) {
    if (is_valid_utf8(s) && s.find('\0') == std::string::npos) return s;

    std::string out;
    out.reserve(s.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();
    const char* replacement = "\xEF\xBF\xBD"; // U+FFFD

    while (p < end) {
        unsigned char c = *p;
        if (c == 0) {
            ++p;
            continue;
        }
        uint32_t cp = 0;
        size_t len = 0;
        size_t remain = static_cast<size_t>(end - p);
        if (!decode_utf8(p, remain, cp, len)) {
            out.append(replacement);
            ++p;
            continue;
        }
        out.append(reinterpret_cast<const char*>(p), len);
        p += len;
    }
    return out;
}

size_t utf8_display_width(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0, width = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        size_t len = 0;
        if (!decode_utf8(p + i, s.size() - i, cp, len)) {
            ++i;
            ++width;
            continue;
        }
        width += static_cast<size_t>(codepoint_width(cp));
        i += len;
    }
    return width;
}

std::string utf8_prefix_columns(const std::string& s, size_t columns) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0, width = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        size_t len = 0;
        if (!decode_utf8(p + i, s.size() - i, cp, len)) {
            if (width == columns) break;
            ++i;
            ++width;
            continue;
        }
        size_t cw = static_cast<size_t>(codepoint_width(cp));
        if (width + cw > columns) break;
        width += cw;
        i += len;
    }
    return s.substr(0, i);
}

void sanitize_json(nlohmann::json& j) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if (!is_valid_utf8(s) || s.find('\0') != std::string::npos)
            j = sanitize_utf8(s);
        return;
    }
    if (j.is_array()) {
        for (auto& e : j) sanitize_json(e);
        return;
    }
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) sanitize_json(it.value());
    }
}

bool looks_binary(const std::string& s, size_t sample) {
    size_t n = std::min(s.size(), sample);
    if (n == 0) return false;
    size_t nuls = 0;
    size_t nontext = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0) {
            ++nuls;
            continue;
        }
        if (c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1B)) {
            ++nontext;
        }
    }
    if (nuls > 0) return true;
    return nontext * 10 > n * 3;
}

}  // namespace util
