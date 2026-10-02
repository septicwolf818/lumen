#include "util.hpp"
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <pwd.h>

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
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return false;
        out << data;
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
            // Must be exactly base or base + '/' + something: a plain prefix
            // match would let "/work/app-evil" in through base "/work/app",
            // and indexing past the end of a shorter pstr is undefined.
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
    // Read once: NO_COLOR does not change while the program runs.
    static const bool set = [] {
        const char* v = std::getenv("NO_COLOR");
        return v != nullptr && *v != '\0';
    }();
    return set;
}

uint64_t steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool is_valid_utf8(const std::string& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();
    while (p < end) {
        unsigned char c = *p;
        if (c <= 0x7F) {
            ++p;
            continue;
        }
        int need = 0;
        if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else return false;
        if (p + need >= end) return false;
        if (need == 1 && c < 0xC2) return false;
        if (need == 3 && c > 0xF4) return false;
        for (int i = 1; i <= need; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
        }
        p += 1 + need;
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
        if (c <= 0x7F) {
            out.push_back(static_cast<char>(c));
            ++p;
            continue;
        }
        int need = 0;
        if ((c & 0xE0) == 0xC0) need = 1;
        else if ((c & 0xF0) == 0xE0) need = 2;
        else if ((c & 0xF8) == 0xF0) need = 3;
        else {
            out.append(replacement);
            ++p;
            continue;
        }
        if (p + need >= end) {
            out.append(replacement);
            break;
        }
        bool ok = true;
        if (need == 1 && c < 0xC2) ok = false;
        if (need == 3 && c > 0xF4) ok = false;
        for (int i = 1; i <= need && ok; ++i) {
            if ((p[i] & 0xC0) != 0x80) ok = false;
        }
        if (!ok) {
            out.append(replacement);
            ++p;
            continue;
        }
        out.append(reinterpret_cast<const char*>(p), static_cast<size_t>(1 + need));
        p += 1 + need;
    }
    return out;
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
