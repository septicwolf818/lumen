#include "markdown.hpp"

#include "keywords_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace md {
namespace {

constexpr size_t npos = std::string::npos;

bool is_space(char c) { return c == ' ' || c == '\t'; }

std::string rtrim(std::string s) {
    while (!s.empty() && is_space(s.back())) s.pop_back();
    return s;
}
std::string ltrim(std::string s) {
    size_t i = 0;
    while (i < s.size() && is_space(s[i])) ++i;
    return s.substr(i);
}
std::string trim(std::string s) { return ltrim(rtrim(s)); }

size_t utf8_char_len(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t utf8_len(const std::string& s) {
    size_t i = 0, n = 0;
    while (i < s.size()) {
        size_t l = utf8_char_len(static_cast<unsigned char>(s[i]));
        if (i + l > s.size()) l = 1;
        i += l;
        ++n;
    }
    return n;
}

std::string utf8_prefix(const std::string& s, size_t n) {
    size_t i = 0, c = 0;
    while (i < s.size() && c < n) {
        size_t l = utf8_char_len(static_cast<unsigned char>(s[i]));
        if (i + l > s.size()) l = 1;
        i += l;
        ++c;
    }
    return s.substr(0, i);
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    for (auto& l : out)
        if (!l.empty() && l.back() == '\r') l.pop_back();
    return out;
}

// ---------------------------------------------------------------- inline ---

Inline text_node(const std::string& t) {
    Inline in;
    in.kind = Inline::Text;
    in.text = t;
    return in;
}

std::vector<Inline> parse_inlines(const std::string& s);

// Defined near the code lexer below; declared here for inline-code language
// detection during parsing.
bool lang_known(const std::string& raw);

bool escape_char(char c) {
    static const std::string special = "\\`*_{}[]()#+-.!>~|";
    return special.find(c) != npos;
}

std::vector<Inline> parse_inlines(const std::string& s) {
    std::vector<Inline> out;
    std::string buf;
    auto flush = [&]() {
        if (!buf.empty()) {
            out.push_back(text_node(buf));
            buf.clear();
        }
    };
    // A span of text can be wrapped in emphasis only when it neither starts
    // nor ends with whitespace ("2 * 3 * 4" must stay literal).
    auto flanked = [](const std::string& inner) {
        return !inner.empty() && inner.front() != ' ' && inner.front() != '\t' &&
               inner.front() != '\n' && inner.back() != ' ' && inner.back() != '\t' &&
               inner.back() != '\n';
    };
    auto alnum = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)); };
    // Intraword underscores never open emphasis, so snake_case stays intact.
    auto underscore_opens = [&](size_t p) { return !(p > 0 && alnum(s[p - 1])); };
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == '\\' && i + 1 < n && escape_char(s[i + 1])) {
            buf.push_back(s[i + 1]);
            i += 2;
            continue;
        }
        if (c == '\\' && i + 1 < n && s[i + 1] == '\n') {
            flush();
            Inline b;
            b.kind = Inline::Break;
            out.push_back(b);
            i += 2;
            continue;
        }
        if (c == '\n') {
            if (buf.size() >= 2 && buf[buf.size() - 1] == ' ' && buf[buf.size() - 2] == ' ') {
                while (!buf.empty() && buf.back() == ' ') buf.pop_back();
                flush();
                Inline b;
                b.kind = Inline::Break;
                out.push_back(b);
            } else {
                buf.push_back(' ');
            }
            ++i;
            continue;
        }
        if (c == '`') {
            size_t run = 1;
            while (i + run < n && s[i + run] == '`') ++run;
            std::string ticks(run, '`');
            size_t close = s.find(ticks, i + run);
            if (close != npos) {
                std::string content = s.substr(i + run, close - (i + run));
                if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ' &&
                    content.find_first_not_of(' ') != npos)
                    content = content.substr(1, content.size() - 2);
                flush();
                Inline in;
                in.kind = Inline::Code;
                // Optional language hint: "```python code```" or "`python: code`".
                // Only split when the leading token is a known language, so
                // ordinary code like `a: b` is left untouched.
                size_t sep = content.find_first_of(": \t");
                if (sep != npos && sep > 0) {
                    std::string lang = content.substr(0, sep);
                    if (lang_known(lang)) {
                        in.lang = lang;
                        size_t rest = content.find_first_not_of(": \t", sep);
                        content = (rest == npos) ? std::string() : content.substr(rest);
                    }
                }
                in.text = content;
                out.push_back(std::move(in));
                i = close + run;
                continue;
            }
        }
        if (c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) {
            size_t open = (c == '!') ? i + 1 : i;
            size_t close = s.find(']', open + 1);
            if (close != npos && close + 1 < n && s[close + 1] == '(') {
                size_t paren = s.find(')', close + 2);
                if (paren != npos) {
                    std::string label = s.substr(open + 1, close - (open + 1));
                    std::string url = s.substr(close + 2, paren - (close + 2));
                    size_t sp = url.find_first_of(" \t");
                    if (sp != npos) url = url.substr(0, sp);
                    flush();
                    Inline in;
                    in.kind = Inline::Link;
                    in.text = label;
                    in.url = url;
                    out.push_back(in);
                    i = paren + 1;
                    continue;
                }
            }
        }
        if (c == '<') {
            size_t gt = s.find('>', i + 1);
            if (gt != npos) {
                std::string inner = s.substr(i + 1, gt - (i + 1));
                if (inner.find("://") != npos || inner.find('@') != npos) {
                    flush();
                    Inline in;
                    in.kind = Inline::Link;
                    in.text = inner;
                    in.url = inner;
                    out.push_back(in);
                    i = gt + 1;
                    continue;
                }
            }
        }
        // ***text*** / ___text___: strong + emphasis.
        if ((c == '*' || c == '_') && i + 2 < n && s[i + 1] == c && s[i + 2] == c &&
            (c != '_' || underscore_opens(i))) {
            std::string d(3, c);
            size_t close = s.find(d, i + 3);
            if (close != npos) {
                std::string inner = s.substr(i + 3, close - (i + 3));
                bool closer_ok = (c != '_') || close + 3 >= n || !alnum(s[close + 3]);
                if (flanked(inner) && closer_ok) {
                    flush();
                    Inline strong;
                    strong.kind = Inline::Strong;
                    Inline em;
                    em.kind = Inline::Emph;
                    em.children = parse_inlines(inner);
                    strong.children.push_back(std::move(em));
                    out.push_back(std::move(strong));
                    i = close + 3;
                    continue;
                }
            }
        }
        if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c &&
            (c != '_' || underscore_opens(i))) {
            std::string d(2, c);
            size_t close = s.find(d, i + 2);
            if (close != npos) {
                std::string inner = s.substr(i + 2, close - (i + 2));
                bool closer_ok = (c != '_') || close + 2 >= n || !alnum(s[close + 2]);
                if (flanked(inner) && closer_ok) {
                    flush();
                    Inline in;
                    in.kind = Inline::Strong;
                    in.children = parse_inlines(inner);
                    out.push_back(std::move(in));
                    i = close + 2;
                    continue;
                }
            }
        }
        if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t close = s.find("~~", i + 2);
            if (close != npos && flanked(s.substr(i + 2, close - (i + 2)))) {
                flush();
                Inline in;
                in.kind = Inline::Strike;
                in.children = parse_inlines(s.substr(i + 2, close - (i + 2)));
                out.push_back(std::move(in));
                i = close + 2;
                continue;
            }
        }
        if ((c == '*' || c == '_') && (c != '_' || underscore_opens(i))) {
            size_t close = s.find(c, i + 1);
            if (close != npos && close > i + 1) {
                std::string inner = s.substr(i + 1, close - (i + 1));
                bool closer_ok = (c != '_') || close + 1 >= n || !alnum(s[close + 1]);
                if (flanked(inner) && closer_ok) {
                    flush();
                    Inline in;
                    in.kind = Inline::Emph;
                    in.children = parse_inlines(inner);
                    out.push_back(std::move(in));
                    i = close + 1;
                    continue;
                }
            }
        }
        if (c == 'h' && (s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0)) {
            size_t j = i;
            while (j < n && !is_space(s[j]) && s[j] != ')' && s[j] != ',') {
                // Trailing sentence punctuation is not part of the URL.
                if ((s[j] == '.' || s[j] == ';' || s[j] == ':' || s[j] == '!' || s[j] == '?') &&
                    (j + 1 >= n || is_space(s[j + 1])))
                    break;
                ++j;
            }
            std::string url = s.substr(i, j - i);
            flush();
            Inline in;
            in.kind = Inline::Link;
            in.text = url;
            in.url = url;
            out.push_back(std::move(in));
            i = j;
            continue;
        }
        buf.push_back(c);
        ++i;
    }
    flush();
    return out;
}

// ----------------------------------------------------------------- block ---

bool parse_heading(const std::string& line, int& level, std::string& text) {
    std::string t = ltrim(line);
    size_t k = 0;
    while (k < t.size() && t[k] == '#') ++k;
    if (k < 1 || k > 6) return false;
    if (k < t.size() && t[k] != ' ' && t[k] != '\t') return false;
    level = static_cast<int>(k);
    text = trim(t.substr(k));
    while (!text.empty() && text.back() == '#') text.pop_back();
    text = trim(text);
    return true;
}

bool is_rule(const std::string& line) {
    std::string t = ltrim(line);
    if (t.empty()) return false;
    char c = t[0];
    if (c != '-' && c != '*' && c != '_') return false;
    int cnt = 0;
    for (char x : t) {
        if (x == c)
            ++cnt;
        else if (x != ' ' && x != '\t')
            return false;
    }
    return cnt >= 3;
}

bool starts_fence(const std::string& line, char& ch, size_t& len, std::string& info) {
    std::string t = ltrim(line);
    if (t.size() < 3) return false;
    char c = t[0];
    if (c != '`' && c != '~') return false;
    size_t k = 0;
    while (k < t.size() && t[k] == c) ++k;
    if (k < 3) return false;
    ch = c;
    len = k;
    info = trim(t.substr(k));
    return true;
}

struct ListMark {
    bool valid = false;
    bool ordered = false;
    size_t indent = 0;
    int start = 1;
    size_t marker_len = 0;
    std::string content;
};

ListMark parse_list_marker(const std::string& line) {
    ListMark m;
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    m.indent = i;
    if (i >= line.size()) return m;
    char c = line[i];
    if (c == '-' || c == '*' || c == '+') {
        if (i + 1 < line.size() && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
            m.valid = true;
            m.ordered = false;
            m.marker_len = 2;
            m.content = ltrim(line.substr(i + 2));
        }
        return m;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        size_t j = i;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) ++j;
        if (j < line.size() && (line[j] == '.' || line[j] == ')') && j + 1 < line.size() &&
            (line[j + 1] == ' ' || line[j + 1] == '\t')) {
            m.valid = true;
            m.ordered = true;
            m.start = std::atoi(line.substr(i, j - i).c_str());
            m.marker_len = (j - i) + 2;
            m.content = ltrim(line.substr(j + 2));
        }
    }
    return m;
}

std::vector<std::string> split_row(const std::string& line) {
    std::string t = trim(line);
    std::vector<std::string> cells;
    std::string cur;
    for (char c : t) {
        if (c == '|') {
            cells.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    cells.push_back(cur);
    if (!cells.empty() && trim(cells.front()).empty()) cells.erase(cells.begin());
    if (!cells.empty() && trim(cells.back()).empty()) cells.pop_back();
    for (auto& c : cells) c = trim(c);
    return cells;
}

bool is_table_delim(const std::string& line, std::vector<int>& aligns) {
    std::string t = trim(line);
    if (t.find('|') == npos) return false;
    auto cells = split_row(t);
    if (cells.empty()) return false;
    std::vector<int> a;
    for (auto cell : cells) {
        if (cell.empty()) return false;
        bool left = cell.front() == ':';
        bool right = cell.back() == ':';
        std::string dash = cell;
        if (left) dash.erase(dash.begin());
        if (right && !dash.empty()) dash.pop_back();
        dash = trim(dash);
        if (dash.empty()) return false;
        for (char x : dash)
            if (x != '-') return false;
        a.push_back(left && right ? 1 : (right ? 2 : 0));
    }
    aligns = a;
    return true;
}

std::vector<Block> parse_blocks(const std::string& src) {
    auto lines = split_lines(src);
    std::vector<Block> blocks;
    size_t i = 0, n = lines.size();
    while (i < n) {
        const std::string& line = lines[i];
        if (trim(line).empty()) {
            ++i;
            continue;
        }

        char fc;
        size_t fl;
        std::string info;
        if (starts_fence(line, fc, fl, info)) {
            Block b;
            b.kind = Block::Code;
            std::string t = trim(info);
            size_t sp = t.find_first_of(" \t");
            b.lang = (sp == npos) ? t : t.substr(0, sp);
            ++i;
            std::string body;
            while (i < n) {
                char cc;
                size_t cl;
                std::string ci;
                if (starts_fence(lines[i], cc, cl, ci) && cc == fc && cl >= fl && trim(ci).empty()) {
                    ++i;
                    break;
                }
                if (!body.empty()) body += '\n';
                body += lines[i];
                ++i;
            }
            b.text = body;
            blocks.push_back(std::move(b));
            continue;
        }

        int level;
        std::string htext;
        if (parse_heading(line, level, htext)) {
            Block b;
            b.kind = Block::Heading;
            b.level = level;
            b.inlines = parse_inlines(htext);
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }

        if (is_rule(line)) {
            Block b;
            b.kind = Block::Rule;
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }

        {
            std::string lt = ltrim(line);
            if (!lt.empty() && lt[0] == '>') {
                std::string content;
                while (i < n) {
                    std::string q = ltrim(lines[i]);
                    if (!q.empty() && q[0] == '>') {
                        std::string rest = q.substr(1);
                        if (!rest.empty() && rest[0] == ' ') rest.erase(rest.begin());
                        if (!content.empty()) content += '\n';
                        content += rest;
                        ++i;
                    } else if (trim(lines[i]).empty()) {
                        std::string nx = (i + 1 < n) ? ltrim(lines[i + 1]) : std::string();
                        if (!nx.empty() && nx[0] == '>') {
                            content += '\n';
                            ++i;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                Block b;
                b.kind = Block::Quote;
                b.children = parse_blocks(content);
                blocks.push_back(std::move(b));
                continue;
            }
        }

        if (line.find('|') != npos && i + 1 < n) {
            std::vector<int> aligns;
            if (is_table_delim(lines[i + 1], aligns)) {
                Block b;
                b.kind = Block::Table;
                auto hdr = split_row(line);
                TableRow hr;
                for (size_t c = 0; c < hdr.size(); ++c) {
                    TableCell cell;
                    cell.inlines = parse_inlines(hdr[c]);
                    cell.align = c < aligns.size() ? aligns[c] : 0;
                    hr.cells.push_back(std::move(cell));
                }
                b.rows.push_back(std::move(hr));
                i += 2;
                while (i < n && !trim(lines[i]).empty() && trim(lines[i]).find('|') != npos) {
                    // A cell may open a ``` fence that continues on following
                    // lines. GFM tables are single-line, but code in a table is
                    // common enough to support: gather until the closing fence
                    // and store it as a code cell.
                    auto cells0 = split_row(lines[i]);
                    int fence_ci = -1;
                    std::string fence_lang;
                    for (size_t c = 0; c < cells0.size(); ++c) {
                        std::string t = trim(cells0[c]);
                        if (t.rfind("```", 0) == 0 && t.find("```", 3) == npos) {
                            fence_ci = static_cast<int>(c);
                            fence_lang = trim(t.substr(3));
                            break;
                        }
                    }
                    if (fence_ci >= 0) {
                        std::string code;
                        ++i;
                        while (i < n) {
                            std::string t = trim(lines[i]);
                            if (t.rfind("```", 0) == 0) { ++i; break; }
                            if (!code.empty()) code += '\n';
                            code += lines[i];
                            ++i;
                        }
                        TableRow r;
                        for (size_t c = 0; c < static_cast<size_t>(fence_ci); ++c) {
                            TableCell pre;
                            pre.inlines = parse_inlines(cells0[c]);
                            pre.align = c < aligns.size() ? aligns[c] : 0;
                            r.cells.push_back(std::move(pre));
                        }
                        TableCell& cell = r.cells.emplace_back();
                        size_t sp = fence_lang.find_first_of(" \t");
                        cell.code_lang = sp == npos ? fence_lang : fence_lang.substr(0, sp);
                        cell.code_text = code;
                        cell.align = fence_ci < static_cast<int>(aligns.size()) ? aligns[fence_ci] : 0;
                        b.rows.push_back(std::move(r));
                        continue;
                    }
                    TableRow r;
                    for (size_t c = 0; c < cells0.size(); ++c) {
                        TableCell cell;
                        cell.inlines = parse_inlines(cells0[c]);
                        cell.align = c < aligns.size() ? aligns[c] : 0;
                        r.cells.push_back(std::move(cell));
                    }
                    b.rows.push_back(std::move(r));
                    ++i;
                }
                blocks.push_back(std::move(b));
                continue;
            }
        }

        {
            ListMark first = parse_list_marker(line);
            if (first.valid) {
                bool ordered = first.ordered;
                size_t base = first.indent;
                Block b;
                b.kind = ordered ? Block::OrderedList : Block::BulletList;
                b.level = first.start;
                while (i < n) {
                    ListMark m = parse_list_marker(lines[i]);
                    if (!m.valid || m.indent != base || m.ordered != ordered) break;
                    std::string content = m.content;
                    ++i;
                    while (i < n) {
                        if (trim(lines[i]).empty()) {
                            size_t j = i + 1;
                            while (j < n && trim(lines[j]).empty()) ++j;
                            if (j < n) {
                                ListMark nm = parse_list_marker(lines[j]);
                                if (nm.valid && nm.indent == base) break;
                                content += '\n';
                                ++i;
                                continue;
                            }
                            break;
                        }
                        ListMark nm = parse_list_marker(lines[i]);
                        size_t ind = 0;
                        while (ind < lines[i].size() && lines[i][ind] == ' ') ++ind;
                        if (nm.valid && nm.indent <= base) break;
                        if (ind <= base) break;
                        size_t strip = std::min(base + 2, lines[i].size());
                        size_t lead = 0;
                        while (lead < lines[i].size() && lines[i][lead] == ' ' && lead < strip) ++lead;
                        content += '\n';
                        content += lines[i].substr(lead);
                        ++i;
                    }
                    ListItem item;
                    item.blocks = parse_blocks(content);
                    b.items.push_back(std::move(item));
                }
                blocks.push_back(std::move(b));
                continue;
            }
        }

        {
            size_t start = i;
            std::string para;
            while (i < n && !trim(lines[i]).empty()) {
                std::string lt = ltrim(lines[i]);
                char cf;
                size_t cf_len;
                std::string cf_info;
                int lv;
                std::string ht;
                std::vector<int> al;
                if (starts_fence(lines[i], cf, cf_len, cf_info)) break;
                if (parse_heading(lines[i], lv, ht)) break;
                if (is_rule(lines[i])) break;
                if (!lt.empty() && lt[0] == '>') break;
                if (parse_list_marker(lines[i]).valid) break;
                if (i + 1 < n && lines[i].find('|') != npos && is_table_delim(lines[i + 1], al)) break;
                if (!para.empty()) para += '\n';
                para += lines[i];
                ++i;
            }
            if (i == start) {
                para = lines[i];
                ++i;
            }
            Block b;
            b.kind = Block::Paragraph;
            b.inlines = parse_inlines(para);
            blocks.push_back(std::move(b));
            continue;
        }
    }
    return blocks;
}

// ---------------------------------------------------------------- render ---

struct WrapState {
    int width = 80;
    Line cur;
    Lines out;
    int col = 0;
    int pending = 0;

    void append(const std::string& t, int style) {
        if (t.empty()) return;
        if (!cur.empty() && cur.back().style == style)
            cur.back().text += t;
        else
            cur.push_back({t, style});
    }
    void emit_pending() {
        if (pending > 0 && col > 0) {
            append(std::string(static_cast<size_t>(pending), ' '), NONE);
            col += pending;
        }
        pending = 0;
    }
    void newline() {
        out.push_back(cur);
        cur.clear();
        col = 0;
        pending = 0;
    }
    void finish() {
        out.push_back(cur);
        cur.clear();
        col = 0;
        pending = 0;
    }
};

void put_word(WrapState& s, const std::string& w, int style) {
    size_t len = utf8_len(w);
    if (s.col > 0 && s.col + s.pending + static_cast<int>(len) > s.width) {
        s.newline();
        s.pending = 0;
    }
    s.emit_pending();
    if (s.col + static_cast<int>(len) <= s.width) {
        s.append(w, style);
        s.col += static_cast<int>(len);
        return;
    }
    size_t i = 0;
    while (i < w.size()) {
        if (s.col >= s.width) s.newline();
        size_t l = utf8_char_len(static_cast<unsigned char>(w[i]));
        if (i + l > w.size()) l = 1;
        s.append(w.substr(i, l), style);
        ++s.col;
        i += l;
    }
}

void put_text(WrapState& s, const std::string& t, int style) {
    size_t i = 0, n = t.size();
    while (i < n) {
        if (t[i] == ' ') {
            size_t j = i;
            while (j < n && t[j] == ' ') ++j;
            s.pending += static_cast<int>(j - i);
            i = j;
            continue;
        }
        size_t j = i;
        while (j < n && t[j] != ' ') ++j;
        put_word(s, t.substr(i, j - i), style);
        i = j;
    }
}

// Forward declaration: the code lexer is defined after the inline renderer.
Line highlight(const std::string& line, const std::string& lang, bool& in_block);

void render_inlines(const std::vector<Inline>& in, WrapState& s, int style) {
    for (const auto& x : in) {
        switch (x.kind) {
            case Inline::Text: put_text(s, x.text, style); break;
            case Inline::Emph: render_inlines(x.children, s, style | ITALIC); break;
            case Inline::Strong: render_inlines(x.children, s, style | BOLD); break;
            case Inline::Strike: render_inlines(x.children, s, style | STRIKE); break;
            case Inline::Code:
                // A language-tagged inline span (inside a table cell or a
                // paragraph) is highlighted with the code lexer, one source
                // line at a time so multi-line snippets keep comment state.
                if (!x.lang.empty()) {
                    bool in_block = false;
                    std::string rest = x.text;
                    size_t start = 0;
                    while (start <= rest.size()) {
                        size_t nl = rest.find('\n', start);
                        std::string line = (nl == npos) ? rest.substr(start)
                                                        : rest.substr(start, nl - start);
                        for (const auto& sp : highlight(line, x.lang, in_block))
                            put_text(s, sp.text, sp.style);
                        if (nl == npos) break;
                        s.newline();
                        start = nl + 1;
                    }
                } else {
                    put_text(s, x.text, style | CODE);
                }
                break;
            case Inline::Link:
                put_text(s, x.text, style | LINK);
                if (!x.url.empty() && x.url != x.text) {
                    s.pending += 1;
                    put_text(s, "(" + x.url + ")", style | MARKER);
                }
                break;
            case Inline::Break: s.newline(); break;
        }
    }
}

using detail::LangProfile;

// Lower-case the fence info string and resolve it via the embedded keyword
// data. Unknown languages return nullptr (rendered as a plain code block).
const LangProfile* lang_profile(const std::string& raw) {
    std::string l = raw;
    for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return detail::lookup_language(l);
}

bool lang_known(const std::string& raw) { return lang_profile(raw) != nullptr; }

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool in_list(const std::vector<std::string>& v, std::string w, bool ci) {
    if (ci)
        for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return std::find(v.begin(), v.end(), w) != v.end();
}

// Tokenize one line of code into styled spans. Identifier classification:
// keyword > constant > type > builtin > function (followed by '(') > variable.
// in_block carries the multi-line comment state across lines: true means the
// line starts inside a block comment, and it is updated to reflect how the line
// ends.
Line highlight(const std::string& line, const std::string& lang, bool& in_block) {
    Line out;
    const LangProfile* p = lang_profile(lang);
    if (!p) {
        out.push_back({line, CODE_BLOCK});
        return out;
    }
    const bool has_block = !p->block_comment_open.empty() && !p->block_comment_close.empty();
    const bool has_line = !p->line_comment.empty();
    bool ci = p->case_insensitive;
    auto is_kw = [&](const std::string& w) { return in_list(p->keywords, w, ci); };
    auto is_const = [&](const std::string& w) { return in_list(p->constants, w, ci); };
    auto is_type = [&](const std::string& w) { return in_list(p->types, w, ci); };
    auto is_builtin = [&](const std::string& w) { return in_list(p->builtins, w, false); };

    size_t i = 0, n = line.size();
    bool at_line_start = true;  // preprocessor for '#' languages
    while (i < n) {
        // Continuing a block comment from a previous line.
        if (in_block) {
            size_t close = has_block ? line.find(p->block_comment_close, i) : npos;
            if (close == npos) {
                out.push_back({line.substr(i), CODE_BLOCK | SYN_COMMENT});
                return out;
            }
            size_t end = close + p->block_comment_close.size();
            out.push_back({line.substr(i, end - i), CODE_BLOCK | SYN_COMMENT});
            i = end;
            in_block = false;
            at_line_start = false;
            continue;
        }

        char c = line[i];

        // A block comment opener: consume to its close (same line) or to EOL.
        if (has_block && line.compare(i, p->block_comment_open.size(),
                                      p->block_comment_open) == 0) {
            size_t close = line.find(p->block_comment_close,
                                     i + p->block_comment_open.size());
            if (close == npos) {
                out.push_back({line.substr(i), CODE_BLOCK | SYN_COMMENT});
                in_block = true;
                return out;
            }
            size_t end = close + p->block_comment_close.size();
            out.push_back({line.substr(i, end - i), CODE_BLOCK | SYN_COMMENT});
            i = end;
            at_line_start = false;
            continue;
        }

        // Preprocessor directive for '#' languages (Python decorators too).
        if (p->python_like && c == '#' && at_line_start) {
            out.push_back({line.substr(i), CODE_BLOCK | SYN_PREPROC});
            break;
        }
        if (p->python_like && c == '@' && at_line_start) {
            size_t j = i + 1;
            while (j < n && ident_char(line[j])) ++j;
            out.push_back({line.substr(i, j - i), CODE_BLOCK | SYN_ATTRIBUTE});
            i = j;
            continue;
        }
        if (has_line && line.compare(i, p->line_comment.size(), p->line_comment) == 0) {
            out.push_back({line.substr(i), CODE_BLOCK | SYN_COMMENT});
            break;
        }

        if (c == '"' || c == '\'' || c == '`') {
            char q = c;
            size_t j = i + 1;
            while (j < n && line[j] != q) {
                if (line[j] == '\\' && j + 1 < n) ++j;
                ++j;
            }
            if (j < n) ++j;
            out.push_back({line.substr(i, j - i), CODE_BLOCK | SYN_STRING});
            i = j;
            at_line_start = false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                             line[j] == '.' || line[j] == '_' || line[j] == 'x' ||
                             line[j] == 'X'))
                ++j;
            out.push_back({line.substr(i, j - i), CODE_BLOCK | SYN_NUMBER});
            i = j;
            at_line_start = false;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < n && ident_char(line[j])) ++j;
            std::string w = line.substr(i, j - i);
            int style;
            if (is_kw(w))
                style = SYN_KEYWORD;
            else if (is_const(w))
                style = SYN_CONSTANT;
            else if (is_type(w))
                style = SYN_TYPE;
            else if (is_builtin(w))
                style = SYN_BUILTIN;
            else if (j < n && line[j] == '(')
                style = SYN_FUNCTION;
            else if (j == n || line[j] == ',' || line[j] == ')' || line[j] == '(')
                style = SYN_VARIABLE;
            else
                style = SYN_VARIABLE;
            out.push_back({w, CODE_BLOCK | style});
            i = j;
            at_line_start = false;
            continue;
        }
        // Operators and punctuation, but never swallow the start of a block
        // comment opener (e.g. HTML "<!--" would otherwise be eaten as a run
        // of operator characters).
        static const std::string ops = "+-*/%=<>!&|^~?:.#@";
        if (ops.find(c) != std::string::npos) {
            size_t j = i;
            while (j < n && ops.find(line[j]) != std::string::npos) {
                if (has_block && j > i &&
                    line.compare(j, p->block_comment_open.size(),
                                 p->block_comment_open) == 0)
                    break;
                ++j;
            }
            out.push_back({line.substr(i, j - i), CODE_BLOCK | SYN_OPERATOR});
            i = j;
            at_line_start = false;
            continue;
        }
        out.push_back({std::string(1, c), CODE_BLOCK});
        i += 1;
        at_line_start = false;
    }
    return out;
}

void render_block(const Block& b, int width, Lines& out);

void render_blocks(const std::vector<Block>& blocks, int width, Lines& out) {
    for (size_t k = 0; k < blocks.size(); ++k) {
        if (k > 0) out.push_back(Line{});
        render_block(blocks[k], width, out);
    }
}

void render_table(const Block& b, int width, Lines& out) {
    if (b.rows.empty()) return;
    size_t ncol = 0;
    for (const auto& r : b.rows) ncol = std::max(ncol, r.cells.size());
    if (ncol == 0) return;

    std::vector<std::vector<Line>> rendered(b.rows.size());
    std::vector<size_t> cw(ncol, 0);
    for (size_t ri = 0; ri < b.rows.size(); ++ri) {
        rendered[ri].resize(ncol);
        for (size_t ci = 0; ci < ncol; ++ci) {
            Line l;
            if (ci < b.rows[ri].cells.size() && !b.rows[ri].cells[ci].code_lang.empty()) {
                // Code cell: highlight the first line of the block so the grid
                // stays single-line (GFM cells cannot span rows).
                const TableCell& cell = b.rows[ri].cells[ci];
                std::string first = cell.code_text.substr(0, cell.code_text.find('\n'));
                bool in_block = false;
                l = highlight(first, cell.code_lang, in_block);
            } else {
                WrapState s;
                s.width = 1000000;
                if (ci < b.rows[ri].cells.size())
                    render_inlines(b.rows[ri].cells[ci].inlines, s, NONE);
                s.finish();
                l = s.out.empty() ? Line{} : std::move(s.out[0]);
            }
            size_t w = 0;
            for (auto& sp : l) w += utf8_len(sp.text);
            cw[ci] = std::max(cw[ci], w);
            rendered[ri][ci] = std::move(l);
        }
    }

    auto total = [&]() {
        size_t t = 1;
        for (auto w : cw) t += w + 3;
        return t;
    };
    while (total() > static_cast<size_t>(width)) {
        size_t wi = 0;
        for (size_t k = 1; k < ncol; ++k)
            if (cw[k] > cw[wi]) wi = k;
        if (cw[wi] <= 3) break;
        cw[wi]--;
    }

    auto border = [&]() {
        std::string s = "+";
        for (size_t k = 0; k < ncol; ++k) s += std::string(cw[k] + 2, '-') + "+";
        return Line{{s, MARKER}};
    };

    out.push_back(border());
    for (size_t ri = 0; ri < b.rows.size(); ++ri) {
        Line row;
        row.push_back({"|", MARKER});
        for (size_t ci = 0; ci < ncol; ++ci) {
            int align = (ci < b.rows[ri].cells.size()) ? b.rows[ri].cells[ci].align : 0;
            Line& cell = rendered[ri][ci];
            size_t w = 0;
            for (auto& sp : cell) w += utf8_len(sp.text);
            size_t pad = cw[ci] > w ? cw[ci] - w : 0;
            size_t lp = 0, rp = 0;
            if (align == 2)
                lp = pad;
            else if (align == 1) {
                lp = pad / 2;
                rp = pad - lp;
            } else {
                rp = pad;
            }
            row.push_back({" ", NONE});
            if (lp) row.push_back({std::string(lp, ' '), NONE});
            size_t used = 0;
            for (auto& sp : cell) {
                if (used >= cw[ci]) break;
                std::string t = sp.text;
                size_t tl = utf8_len(t);
                if (used + tl > cw[ci]) {
                    t = utf8_prefix(t, cw[ci] - used);
                    tl = utf8_len(t);
                }
                row.push_back({t, sp.style});
                used += tl;
            }
            if (rp) row.push_back({std::string(rp, ' '), NONE});
            row.push_back({" ", NONE});
            row.push_back({"|", MARKER});
        }
        out.push_back(std::move(row));
        if (ri == 0) out.push_back(border());
    }
}

void render_block(const Block& b, int width, Lines& out) {
    switch (b.kind) {
        case Block::Paragraph: {
            WrapState s;
            s.width = width;
            render_inlines(b.inlines, s, NONE);
            s.finish();
            for (auto& l : s.out) out.push_back(std::move(l));
            break;
        }
        case Block::Heading: {
            WrapState s;
            s.width = width;
            render_inlines(b.inlines, s, BOLD | HEADING);
            s.finish();
            for (auto& l : s.out) out.push_back(std::move(l));
            break;
        }
        case Block::Code: {
            // Full-width rule with the language inline, e.g. "python ------...".
            std::string header = b.lang.empty() ? "code" : b.lang;
            size_t hlen = utf8_len(header);
            Line rule;
            rule.push_back({header, CODE_BLOCK | MARKER});
            rule.push_back({" ", MARKER});
            if (hlen + 1 < static_cast<size_t>(width))
                rule.push_back({std::string(static_cast<size_t>(width) - hlen - 1, '-'), MARKER});
            out.push_back(std::move(rule));

            auto raw = split_lines(b.text);
            bool in_block = false;  // multi-line comment state across source lines
            for (auto& rl : raw) {
                size_t pos = 0;
                if (rl.empty()) {
                    out.push_back(Line{});
                    continue;
                }
                bool first_chunk = true;
                while (pos < rl.size()) {
                    std::string chunk;
                    size_t cnt = 0;
                    while (pos < rl.size() && cnt < static_cast<size_t>(width)) {
                        size_t l = utf8_char_len(static_cast<unsigned char>(rl[pos]));
                        if (pos + l > rl.size()) l = 1;
                        chunk += rl.substr(pos, l);
                        pos += l;
                        ++cnt;
                    }
                    // Only the first chunk of a source line continues the
                    // previous line's comment state; wrapped continuation
                    // chunks are treated as fresh line text.
                    bool carry = first_chunk && in_block;
                    out.push_back(highlight(chunk, b.lang, carry));
                    if (first_chunk) in_block = carry;
                    first_chunk = false;
                }
            }
            break;
        }
        case Block::Quote: {
            Lines inner;
            render_blocks(b.children, std::max(1, width - 2), inner);
            for (auto& l : inner) {
                if (l.empty()) {
                    out.push_back(Line{});
                    continue;
                }
                Line nl;
                nl.push_back({"│ ", QUOTE});
                for (auto& sp : l) nl.push_back(std::move(sp));
                out.push_back(std::move(nl));
            }
            break;
        }
        case Block::BulletList:
        case Block::OrderedList: {
            int num = (b.kind == Block::OrderedList) ? (b.level > 0 ? b.level : 1) : 0;
            for (auto& item : b.items) {
                Lines inner;
                render_blocks(item.blocks, std::max(1, width - 2), inner);
                std::string marker = (b.kind == Block::BulletList) ? "- " : (std::to_string(num++) + ". ");
                size_t mlen = utf8_len(marker);
                bool first = true;
                for (auto& l : inner) {
                    if (l.empty()) {
                        out.push_back(Line{});
                        continue;
                    }
                    Line nl;
                    if (first) {
                        nl.push_back({marker, MARKER});
                        first = false;
                    } else {
                        nl.push_back({std::string(mlen, ' '), NONE});
                    }
                    for (auto& sp : l) nl.push_back(std::move(sp));
                    out.push_back(std::move(nl));
                }
            }
            break;
        }
        case Block::Rule: {
            std::string r;
            for (int k = 0; k < width; ++k) r += "─";
            out.push_back(Line{{r, MARKER}});
            break;
        }
        case Block::Table:
            render_table(b, width, out);
            break;
    }
}

}  // namespace

std::vector<Block> parse(const std::string& src) { return parse_blocks(src); }

Lines render(const std::vector<Block>& blocks, int width) {
    if (width < 8) width = 8;
    Lines out;
    render_blocks(blocks, width, out);
    return out;
}

Lines render(const std::string& src, int width) { return render(parse(src), width); }

}  // namespace md
