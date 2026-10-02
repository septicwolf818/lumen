#pragma once

// Slash-command completion.
// Pure, header-only logic so it can be unit tested without a TTY.

#include <cstddef>
#include <string>
#include <vector>

namespace completion {

// Every command name accepted by Agent::handle_command (including aliases).
inline const std::vector<std::string>& command_names() {
    static const std::vector<std::string> names = {
        "/help", "/?",
        "/quit", "/exit",
        "/model",
        "/session",
        "/cd",
        "/pwd",
        "/clear",
        "/compact",
        "/tools",
        "/config",
        "/reload",
    };
    return names;
}

// Subcommands accepted after /session
inline const std::vector<std::string>& session_subcommands() {
    static const std::vector<std::string> subs = {
        "new", "list", "load", "delete", "rename",
    };
    return subs;
}

struct Target {
    bool valid = false;                 // completion applies at the cursor
    size_t start = 0;                   // token start offset in input
    size_t end = 0;                     // token end offset (exclusive)
    std::vector<std::string> matches;   // candidates (may be empty -> no match)
};

namespace detail {

inline bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

inline bool iprefix(const std::string& word, const std::string& candidate) {
    if (word.size() > candidate.size()) return false;
    for (size_t i = 0; i < word.size(); ++i) {
        char x = word[i], y = candidate[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

inline std::vector<std::string> filter(const std::vector<std::string>& pool,
                                       const std::string& word) {
    std::vector<std::string> out;
    for (const auto& c : pool) {
        if (iprefix(word, c)) out.push_back(c);
    }
    return out;
}

}  // namespace detail

// Find the token containing `cursor` and, when it is a completable position,
// the candidates for it. Completable positions are:
//   - the first token, when the input starts with '/'
//   - the second token of "/session ..."
inline Target analyze(const std::string& input, size_t cursor) {
    Target t;
    if (input.empty() || input[0] != '/') return t;
    if (cursor > input.size()) cursor = input.size();

    // Collect tokens (offsets) separated by single spaces.
    struct Tok { size_t start; size_t end; };
    std::vector<Tok> toks;
    size_t i = 0;
    while (i < input.size()) {
        while (i < input.size() && input[i] == ' ') ++i;
        if (i >= input.size()) break;
        size_t s = i;
        while (i < input.size() && input[i] != ' ') ++i;
        toks.push_back({s, i});
    }
    if (toks.empty()) return t;

    // Token the cursor sits in (cursor at a token end belongs to that token).
    int idx = -1;
    for (size_t k = 0; k < toks.size(); ++k) {
        if (cursor >= toks[k].start && cursor <= toks[k].end) {
            idx = static_cast<int>(k);
            break;
        }
    }
    if (idx < 0) {
        // Cursor past the last token on whitespace: an empty token, so that
        // e.g. "/session <Tab>" offers every subcommand.
        size_t from = toks.back().end;
        bool spaces = true;
        for (size_t k = from; k < cursor; ++k) {
            if (input[k] != ' ') { spaces = false; break; }
        }
        if (!spaces) return t;
        toks.push_back({cursor, cursor});
        idx = static_cast<int>(toks.size()) - 1;
    }

    const Tok& tok = toks[static_cast<size_t>(idx)];
    std::string word = input.substr(tok.start, tok.end - tok.start);
    t.start = tok.start;
    t.end = tok.end;

    if (idx == 0) {
        t.valid = true;
        t.matches = detail::filter(command_names(), word);
        return t;
    }
    if (idx == 1) {
        std::string first = input.substr(toks[0].start, toks[0].end - toks[0].start);
        if (detail::ieq(first, "/session")) {
            t.valid = true;
            t.matches = detail::filter(session_subcommands(), word);
            return t;
        }
    }
    return t;  // valid = false: not a completable position
}

inline std::string common_prefix(const std::vector<std::string>& v) {
    if (v.empty()) return "";
    std::string p = v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        size_t n = 0;
        while (n < p.size() && n < v[i].size() && p[n] == v[i][n]) ++n;
        p.resize(n);
        if (p.empty()) break;
    }
    return p;
}

}  // namespace completion
