#include "ui.hpp"
#include "markdown.hpp"
#include "util.hpp"
#include <algorithm>

namespace {

// SGR sequences for UiStyle. Only bold/dim and basic colours 31-36 (black and
// white are never used), which the Linux console renders; keeps every line
// compatible with TERM=linux.
// Returns an empty string under NO_COLOR, so write_styled emits plain text.
std::string sgr(int style) {
    if (util::no_color()) return "";
    std::string params;
    auto add = [&](const char* p) {
        if (!params.empty()) params += ';';
        params += p;
    };
    if (style & STYLE_BOLD) add("1");
    if (style & STYLE_DIM) add("2");
    if (style & STYLE_UNDERLINE) add("4");
    if (style & STYLE_REVERSE) add("7");
    if (style & STYLE_RED) add("31");
    if (style & STYLE_GREEN) add("32");
    if (style & STYLE_YELLOW) add("33");
    if (style & STYLE_BLUE) add("34");
    if (style & STYLE_MAGENTA) add("35");
    if (style & STYLE_CYAN) add("36");
    if (params.empty()) return "";
    return "\033[" + params + "m";
}

void write_styled(Terminal& t, int style, const std::string& text) {
    if (text.empty()) return;
    std::string seq = sgr(style);
    if (seq.empty()) {
        t.write(text);
        return;
    }
    t.write(seq);
    t.write(text);
    t.reset_attrs();
}

// Cut to at most n characters (UTF-8 codepoints) without splitting a
// sequence. Truncation must count characters, not bytes: wrap() limits
// lines to n display columns, and a line of n characters can easily be
// more than n bytes - cutting at byte n would drop characters that fit.
std::string utf8_prefix(const std::string& s, size_t n) {
    size_t i = 0;
    size_t count = 0;
    while (i < s.size() && count < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        i += len;
        ++count;
    }
    return s.substr(0, i);
}

// Number of characters (UTF-8 codepoints) in s.
size_t utf8_len(const std::string& s) {
    size_t i = 0;
    size_t count = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        i += len;
        ++count;
    }
    return count;
}

// Scrollback cap.
constexpr size_t kMaxLines = 5000;

constexpr uint64_t kMarqueeMs = 500;

struct StatusChar {
    std::string bytes;
    int style;
};

size_t utf8_seq_len(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

void push_status_chars(std::vector<StatusChar>& out, const std::string& s, int style) {
    size_t i = 0;
    while (i < s.size()) {
        size_t len = utf8_seq_len(static_cast<unsigned char>(s[i]));
        if (i + len > s.size()) len = 1;
        out.push_back({s.substr(i, len), style});
        i += len;
    }
}

std::string sanitize_text(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t col = 0;
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\n') {
            out.push_back('\n');
            col = 0;
            ++i;
            continue;
        }
        if (c == '\t') {
            size_t pad = 4 - (col % 4);
            out.append(pad, ' ');
            col += pad;
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            ++i;
            continue;
        }
        size_t len = utf8_seq_len(c);
        if (i + len > in.size()) len = 1;
        out.append(in, i, len);
        col += 1;
        i += len;
    }
    return out;
}

int map_md_style(int st, bool dim = false) {
    int r = STYLE_NORMAL;
    if (st & md::BOLD) r |= STYLE_BOLD;
    if (st & md::ITALIC) r |= STYLE_YELLOW;
    if (st & md::STRIKE) r |= STYLE_DIM;
    if (st & md::CODE) r |= STYLE_REVERSE;
    if (st & md::HEADING) r |= STYLE_BOLD | STYLE_CYAN;
    if (st & md::LINK) r |= STYLE_UNDERLINE | STYLE_BLUE;
    if (st & md::QUOTE) r |= STYLE_DIM;
    if (st & md::MARKER) r |= STYLE_DIM;

    // Code syntax colours. Only seven usable console foregrounds, so
    // categories are spread across distinct ones, bold marks keywords, and
    // base text stays uncoloured.
    if (st & md::CODE_BLOCK) r |= STYLE_NORMAL;   // base code text: default fg
    if (st & md::SYN_KEYWORD) r |= STYLE_BOLD | STYLE_BLUE;
    if (st & md::SYN_CONSTANT) r |= STYLE_MAGENTA;
    if (st & md::SYN_TYPE) r |= STYLE_YELLOW;
    if (st & md::SYN_BUILTIN) r |= STYLE_GREEN;
    if (st & md::SYN_FUNCTION) r |= STYLE_BOLD | STYLE_CYAN;
    if (st & md::SYN_VARIABLE) r |= STYLE_NORMAL;
    if (st & md::SYN_STRING) r |= STYLE_RED;
    if (st & md::SYN_NUMBER) r |= STYLE_MAGENTA | STYLE_BOLD;
    if (st & md::SYN_COMMENT) r |= STYLE_DIM;
    if (st & md::SYN_OPERATOR) r |= STYLE_NORMAL;  // punctuation: uncoloured
    if (st & md::SYN_PREPROC) r |= STYLE_BOLD | STYLE_GREEN;
    if (st & md::SYN_ATTRIBUTE) r |= STYLE_YELLOW | STYLE_BOLD;

    // Thinking: same highlight, made to read recessed. SGR 2 cannot combine
    // with a colour on the Linux console (faint overrides the foreground,
    // greying everything), so coloured tokens lose bold (bright -> normal),
    // inline code is plain cyan instead of reverse, and the rest is dimmed.
    if (dim) {
        r &= ~STYLE_REVERSE;
        if (st & md::HEADING) {
            r = STYLE_DIM;  // no cyan tint: cyan is reserved for inline code
        } else {
            const int colored = STYLE_RED | STYLE_GREEN | STYLE_YELLOW |
                                STYLE_BLUE | STYLE_MAGENTA | STYLE_CYAN;
            if (r & colored)
                r &= ~(STYLE_BOLD | STYLE_DIM);
            else if (st & md::CODE)
                r = STYLE_CYAN;
            else
                r |= STYLE_DIM;
        }
    }
    return r;
}

}  // namespace

UI::UI(Terminal& term) : term_(term) {}

void UI::set_status(const std::string& model, const std::string& session, const std::string& cwd) {
    status_model_ = model;
    status_session_ = session;
    status_cwd_ = cwd;
    marquee_offset_ = 0;
}

int UI::viewport_rows() const {
    return std::max(1, term_.rows() - 3);  // status + separator + input
}

void UI::push_lines(std::vector<DisplayLine> lines) {
    for (auto& l : lines) lines_.push_back(std::move(l));
    while (lines_.size() > kMaxLines) {
        lines_.pop_front();
        if (stream_base_ > 0) --stream_base_;  // indices shift with the pop
    }
}

void UI::wrap_and_push(const std::string& text, int style) {
    push_lines(wrap_spans({{text, style}}, std::max(10, term_.cols())));
}

std::vector<DisplayLine> UI::wrap_spans(const std::vector<Span>& spans, int width) const {
    std::vector<DisplayLine> out;
    if (width < 1) width = 1;

    struct Token {
        std::string text;
        int style;
        bool nl = false;
    };
    std::vector<Token> tokens;
    size_t tcol = 0;
    for (const auto& sp : spans) {
        size_t i = 0;
        while (i < sp.text.size()) {
            unsigned char c = static_cast<unsigned char>(sp.text[i]);
            if (c == '\n') {
                tokens.push_back({"", sp.style, true});
                tcol = 0;
                ++i;
                continue;
            }
            if (c == '\t') {
                size_t pad = 4 - (tcol % 4);  // expand to the next tab stop
                tokens.push_back({std::string(pad, ' '), sp.style, false});
                tcol += pad;
                ++i;
                continue;
            }
            if (c == ' ') {
                size_t j = i;
                while (j < sp.text.size() && sp.text[j] == ' ') ++j;
                tokens.push_back({std::string(j - i, ' '), sp.style, false});
                tcol += j - i;
                i = j;
                continue;
            }
            if (c < 0x20 || c == 0x7F) {  // controls never reach the terminal
                ++i;
                continue;
            }
            size_t len = utf8_seq_len(c);
            if (i + len > sp.text.size()) len = 1;
            tokens.push_back({sp.text.substr(i, len), sp.style, false});
            ++tcol;
            i += len;
        }
    }

    DisplayLine cur;
    int col = 0;
    int pending = 0;
    auto append = [&](const std::string& t, int style) {
        if (t.empty()) return;
        if (!cur.spans.empty() && cur.spans.back().style == style)
            cur.spans.back().text += t;
        else
            cur.spans.push_back({t, style});
    };
    auto newline = [&]() {
        out.push_back(std::move(cur));
        cur = DisplayLine{};
        col = 0;
        pending = 0;
    };
    auto emit_pending = [&]() {
        if (pending > 0 && col > 0) {
            append(std::string(static_cast<size_t>(pending), ' '), STYLE_NORMAL);
            col += pending;
        }
        pending = 0;
    };

    for (auto& t : tokens) {
        if (t.nl) {
            newline();
            continue;
        }
        if (t.text.find_first_not_of(' ') == std::string::npos) {
            pending += static_cast<int>(utf8_len(t.text));
            continue;
        }
        int len = static_cast<int>(utf8_len(t.text));
        if (col > 0 && col + pending + len > width) {
            newline();
            pending = 0;
        }
        emit_pending();
        if (col + len <= width) {
            append(t.text, t.style);
            col += len;
            continue;
        }
        size_t k = 0;
        while (k < t.text.size()) {
            if (col >= width) newline();
            size_t l = utf8_seq_len(static_cast<unsigned char>(t.text[k]));
            if (k + l > t.text.size()) l = 1;
            append(t.text.substr(k, l), t.style);
            ++col;
            k += l;
        }
    }
    out.push_back(std::move(cur));
    return out;
}

std::vector<DisplayLine> UI::render_markdown(const std::string& text, int width, bool dim) const {
    auto lines = md::render(sanitize_text(text), std::max(10, width));
    std::vector<DisplayLine> out;
    out.reserve(lines.size());
    for (auto& l : lines) {
        DisplayLine dl;
        for (auto& sp : l) dl.spans.push_back({sp.text, map_md_style(sp.style, dim)});
        out.push_back(std::move(dl));
    }
    return out;
}

void UI::append(const std::string& text, int style) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos)
            ? text.substr(start)
            : text.substr(start, nl - start);
        wrap_and_push(line, style);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void UI::append_markdown(const std::string& text) {
    push_lines(render_markdown(text, term_.cols()));
}

void UI::append_thinking(const std::string& text) {
    if (markdown_)
        push_lines(render_markdown(text, term_.cols(), /*dim=*/true));
    else
        append(text, STYLE_DIM);
}

void UI::begin_stream(int style) {
    streaming_ = true;
    stream_style_ = style;
    stream_acc_.clear();
    lines_.push_back(DisplayLine{});
    // stream_token() pops back to this line before re-wrapping.
    stream_base_ = lines_.size() - 1;
}

void UI::stream_token(const std::string& tok) {
    if (!streaming_) {
        begin_stream();
    }
    stream_acc_ += tok;
    // Render Markdown live: the parser is safe on partial input (an unclosed
    // fence renders as code, half-typed emphasis stays literal), so the layout
    // never jumps. Thinking blocks use the same renderer with dim.
    std::vector<DisplayLine> wrapped;
    if (markdown_)
        wrapped = render_markdown(stream_acc_, std::max(10, term_.cols()),
                                  stream_style_ == STYLE_DIM);
    else
        wrapped = wrap_spans({{sanitize_text(stream_acc_), stream_style_}},
                             std::max(10, term_.cols()));
    while (lines_.size() > stream_base_) lines_.pop_back();
    for (auto& dl : wrapped) lines_.push_back(std::move(dl));
    if (lines_.empty()) lines_.push_back(DisplayLine{});
}

void UI::end_stream() {
    // Final render closes any still-open fence cleanly.
    if (markdown_ && !stream_acc_.empty()) {
        auto rendered = render_markdown(stream_acc_, term_.cols(),
                                        stream_style_ == STYLE_DIM);
        while (lines_.size() > stream_base_) lines_.pop_back();
        for (auto& dl : rendered) lines_.push_back(std::move(dl));
    }
    streaming_ = false;
    stream_acc_.clear();
}

void UI::clear_conversation() {
    lines_.clear();
    scroll_offset_ = 0;
    streaming_ = false;
    stream_acc_.clear();
    stream_base_ = 0;
}

void UI::scroll_up(int n) {
    int max_off = static_cast<int>(lines_.size()) - viewport_rows();
    if (max_off < 0) max_off = 0;
    scroll_offset_ = std::min(static_cast<size_t>(max_off), scroll_offset_ + static_cast<size_t>(n));
}

void UI::scroll_down(int n) {
    if (scroll_offset_ <= static_cast<size_t>(n)) scroll_offset_ = 0;
    else scroll_offset_ -= static_cast<size_t>(n);
}

void UI::scroll_page_up() { scroll_up(viewport_rows()); }
void UI::scroll_page_down() { scroll_down(viewport_rows()); }

void UI::set_input(const std::string& s) {
    input_ = s;
    cursor_ = s.size();
}

void UI::set_input(const std::string& s, size_t cursor) {
    input_ = s;
    cursor_ = std::min(cursor, s.size());
}

void UI::input_insert(uint32_t cp) {
    std::string u = utf8_from_cp(cp);
    input_.insert(cursor_, u);
    cursor_ += u.size();
}

void UI::input_backspace() {
    if (cursor_ == 0) return;
    // delete one UTF-8 char before cursor
    size_t i = cursor_;
    do { --i; } while (i > 0 && (static_cast<unsigned char>(input_[i]) & 0xC0) == 0x80);
    input_.erase(i, cursor_ - i);
    cursor_ = i;
}

void UI::input_delete() {
    if (cursor_ >= input_.size()) return;
    size_t i = cursor_;
    size_t len = 1;
    unsigned char c = static_cast<unsigned char>(input_[i]);
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > input_.size()) len = input_.size() - i;
    input_.erase(i, len);
}

void UI::input_left() {
    if (cursor_ == 0) return;
    do { --cursor_; } while (cursor_ > 0 && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80);
}

void UI::input_right() {
    if (cursor_ >= input_.size()) return;
    unsigned char c = static_cast<unsigned char>(input_[cursor_]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    cursor_ = std::min(input_.size(), cursor_ + len);
}

void UI::input_home() { cursor_ = 0; }
void UI::input_end() { cursor_ = input_.size(); }

void UI::input_history_up() {
    if (history_.empty()) return;
    if (history_idx_ < 0) {
        history_stash_ = input_;
        history_idx_ = static_cast<int>(history_.size()) - 1;
    } else if (history_idx_ > 0) {
        --history_idx_;
    }
    input_ = history_[static_cast<size_t>(history_idx_)];
    cursor_ = input_.size();
}

void UI::input_history_down() {
    if (history_idx_ < 0) return;
    if (history_idx_ + 1 >= static_cast<int>(history_.size())) {
        history_idx_ = -1;
        input_ = history_stash_;
    } else {
        ++history_idx_;
        input_ = history_[static_cast<size_t>(history_idx_)];
    }
    cursor_ = input_.size();
}

void UI::push_history(const std::string& s) {
    if (s.empty()) return;
    if (!history_.empty() && history_.back() == s) return;
    history_.push_back(s);
    if (history_.size() > 100) history_.erase(history_.begin());
    history_idx_ = -1;
}

std::string UI::utf8_from_cp(uint32_t cp) {
    std::string r;
    if (cp < 0x80) {
        r += static_cast<char>(cp);
    } else if (cp < 0x800) {
        r += static_cast<char>(0xC0 | (cp >> 6));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        r += static_cast<char>(0xE0 | (cp >> 12));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        r += static_cast<char>(0xF0 | (cp >> 18));
        r += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return r;
}

std::vector<std::pair<std::string, int>> UI::status_parts() const {
    std::vector<std::pair<std::string, int>> parts;
    auto kv = [&parts](const std::string& label, const std::string& value) {
        if (!parts.empty()) parts.push_back({"  ", STYLE_NORMAL});
        parts.push_back({label, STYLE_BOLD | STYLE_CYAN});
        parts.push_back({value, STYLE_NORMAL});
    };
    kv("model:", status_model_.empty() ? "(none)" : status_model_);
    kv("session:", status_session_.empty() ? "(none)" : status_session_);
    if (!status_ctx_.empty()) kv("ctx:", status_ctx_);
    if (!status_cwd_.empty()) kv("cwd:", status_cwd_);
    return parts;
}

void UI::draw_status() {
    term_.move_cursor(1, 1);
    term_.clear_line();

    auto parts = status_parts();
    const int cols = std::max(1, term_.cols());

    size_t total = 0;
    for (const auto& p : parts) total += utf8_len(p.first);

    if (total <= static_cast<size_t>(cols)) {
        marquee_offset_ = 0;
        int avail = cols;
        for (auto& p : parts) {
            if (avail <= 0) break;
            std::string t = utf8_prefix(p.first, static_cast<size_t>(avail));
            write_styled(term_, p.second, t);
            avail -= static_cast<int>(utf8_len(t));
        }
        return;
    }

    std::vector<StatusChar> chars;
    for (const auto& p : parts) push_status_chars(chars, p.first, p.second);
    push_status_chars(chars, "   ", STYLE_DIM);

    const size_t period = chars.size();
    if (period == 0) return;
    marquee_offset_ %= period;

    size_t shown = 0;
    size_t idx = marquee_offset_;
    while (shown < static_cast<size_t>(cols)) {
        const int style = chars[idx].style;
        std::string run;
        while (shown < static_cast<size_t>(cols) && chars[idx].style == style) {
            run += chars[idx].bytes;
            idx = (idx + 1) % period;
            ++shown;
        }
        write_styled(term_, style, run);
    }
}

void UI::tick_marquee() {
    size_t total = 0;
    for (const auto& p : status_parts()) total += utf8_len(p.first);

    if (total <= static_cast<size_t>(std::max(1, term_.cols()))) {
        if (marquee_offset_ != 0) {
            marquee_offset_ = 0;
            redraw();
        }
        return;
    }

    uint64_t now = util::steady_ms();
    if (now - marquee_last_ms_ < kMarqueeMs) return;
    marquee_last_ms_ = now;
    marquee_offset_ += 1;
    redraw();
}

// First conversation line and row count of the window currently shown.
// Shared by the live drawing and the exit dump so both show the same screen.
void UI::viewport_window(int& start, int& rows) const {
    rows = viewport_rows();
    int total = static_cast<int>(lines_.size());
    start = total - rows - static_cast<int>(scroll_offset_);
    if (start < 0) start = 0;
}

void UI::draw_viewport() {
    int start, rows;
    viewport_window(start, rows);
    int total = static_cast<int>(lines_.size());

    for (int i = 0; i < rows; ++i) {
        term_.move_cursor(2 + i, 1);
        term_.clear_line();
        int idx = start + i;
        if (idx >= 0 && idx < total) {
            const auto& dl = lines_[static_cast<size_t>(idx)];
            int avail = term_.cols();  // char-cut; only trims after a shrink
            for (const auto& sp : dl.spans) {
                if (avail <= 0) break;
                std::string t = utf8_prefix(sp.text, static_cast<size_t>(avail));
                write_styled(term_, sp.style, t);
                avail -= static_cast<int>(utf8_len(t));
            }
        }
    }

    term_.move_cursor(term_.rows() - 1, 1);  // separator / scroll / spinner
    term_.clear_line();
    std::string ind;
    if (working_depth_ > 0) {
        static const char kFrames[] = "|/-\\";
        ind = std::string(1, kFrames[spinner_frame_]);
    }
    if (scroll_offset_ > 0) {
        if (!ind.empty()) ind += "  ";
        ind += "[scroll: " + std::to_string(scroll_offset_) + " lines above bottom]";
    }
    if (queue_count_ > 0) {
        if (!ind.empty()) ind += "  ";
        ind += "[queued: " + std::to_string(queue_count_) + "]";
    }
    if (ind.empty()) ind = "---";
    ind = utf8_prefix(ind, static_cast<size_t>(term_.cols()));
    write_styled(term_, STYLE_DIM, ind);
}

void UI::draw_input() {
    term_.move_cursor(term_.rows(), 1);
    term_.clear_line();
    std::string prefix = "> ";
    write_styled(term_, STYLE_BOLD | STYLE_GREEN, prefix);
    int avail = term_.cols() - static_cast<int>(prefix.size());  // window the input
    if (avail < 4) avail = 4;
    std::string shown = input_;
    size_t vis_cursor = cursor_;
    if (static_cast<int>(shown.size()) > avail) {
        size_t start = 0;  // window around the cursor
        if (cursor_ + 1 > static_cast<size_t>(avail))
            start = cursor_ + 1 - static_cast<size_t>(avail);
        shown = input_.substr(start, static_cast<size_t>(avail));
        vis_cursor = cursor_ - start;
    }
    term_.write(shown);
    term_.move_cursor(term_.rows(), static_cast<int>(prefix.size()) + static_cast<int>(vis_cursor) + 1);
}

void UI::redraw() {
    term_.hide_cursor();
    draw_status();
    draw_viewport();
    draw_input();
    term_.show_cursor();
}

// Reprint the on-screen conversation sequentially so it survives exit: a
// no-op inside the alt screen, and a clean last frame on terminals that
// ignored it. Uses only plain Linux console sequences (home, erase, SGR).
void UI::dump_screen() {
    int start, rows;
    viewport_window(start, rows);
    int total = static_cast<int>(lines_.size());
    int width = std::max(10, term_.cols());
    term_.clear_screen();
    term_.move_cursor(1, 1);
    for (int i = 0; i < rows; ++i) {
        int idx = start + i;
        if (idx >= total) break;
        const auto& dl = lines_[static_cast<size_t>(idx)];
        // Re-wrap: the window may have shrunk since the line was appended.
        for (auto& wl : wrap_spans(dl.spans, width)) {
            for (const auto& sp : wl.spans) write_styled(term_, sp.style, sp.text);
            term_.write("\r\n");
        }
    }
}

void UI::farewell() {
    term_.write("\r\n");  // start on a fresh row, past the old shell prompt
    write_styled(term_, STYLE_BOLD | STYLE_GREEN, "lumen: Bye!");
    term_.write(" Session was ");
    write_styled(term_, STYLE_BOLD,
                 status_session_.empty() ? "(none)" : status_session_);
    term_.write("\r\n");
}

void UI::on_resize() {
    term_.update_size();
    redraw();
}

bool UI::confirm(InputReader& in, const std::string& prompt) {
    // Raw mode has OPOST off, so \n will not return to column 1 and a
    // multi-line prompt would staircase. Print all but the last line to the
    // scrollback and keep the question on the input row.
    auto lines = util::split_lines(prompt);
    std::string question = lines.empty() ? prompt : lines.back();
    if (!lines.empty()) lines.pop_back();
    for (const auto& l : lines) append(l, 1);
    if (!lines.empty()) redraw();
    term_.move_cursor(term_.rows(), 1);
    term_.clear_line();
    write_styled(term_, STYLE_BOLD | STYLE_YELLOW, question);  // must stand out
    term_.show_cursor();
    for (;;) {
        if (Terminal::got_sigterm.load()) return false;
        auto ev = in.read(200);
        if (!ev) continue;
        if (ev->key == Key::Char) {
            char c = static_cast<char>(ev->codepoint);
            if (c == 'y' || c == 'Y') return true;
            if (c == 'n' || c == 'N') return false;
        }
        if (ev->key == Key::Enter) return false;  // default is N
        if (ev->key == Key::CtrlC || ev->key == Key::Escape) return false;
        if (ev->key == Key::CtrlQ) return false;  // never swallow Ctrl-Q
    }
}

std::optional<std::string> UI::ask_question(InputReader& in,
                                            const std::string& question,
                                            const std::vector<std::string>& options) {
    const std::string kCustomLabel = "Type your own answer...";
    std::vector<std::string> opts = options;
    if (opts.empty() || opts.back() != kCustomLabel) opts.push_back(kCustomLabel);
    const size_t custom_idx = opts.size() - 1;

    // Flatten to a single display line: newlines/tabs become spaces, other
    // controls are dropped, UTF-8 sequences preserved.
    auto flatten = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\n' || c == '\r' || c == '\t') {
                out.push_back(' ');
                ++i;
                continue;
            }
            if (c < 0x20 || c == 0x7F) {
                ++i;
                continue;
            }
            size_t len = utf8_seq_len(c);
            if (i + len > s.size()) len = 1;
            out.append(s, i, len);
            i += len;
        }
        return out;
    };

    // Byte index -> character index (for cursor placement in UTF-8 text).
    auto char_count_before = [](const std::string& s, size_t bytes) {
        size_t n = 0;
        size_t i = 0;
        while (i < s.size() && i < bytes) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len > s.size()) len = 1;
            i += len;
            ++n;
        }
        return n;
    };

    // Split into UTF-8 characters (each entry is one character's bytes).
    auto split_chars = [](const std::string& s) {
        std::vector<std::string> out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len > s.size()) len = 1;
            out.push_back(s.substr(i, len));
            i += len;
        }
        return out;
    };

    // Preserve any half-typed main input across the popup.
    std::string saved_input = input_;
    size_t saved_cursor = cursor_;
    input_.clear();
    cursor_ = 0;

    size_t sel = 0;
    bool editing = false;
    std::string custom;
    size_t custom_cursor = 0;  // byte index into custom

    auto draw = [&]() {
        if (Terminal::got_sigwinch.exchange(false)) term_.update_size();
        int cols = std::max(10, term_.cols());
        int rows = std::max(8, term_.rows());
        term_.hide_cursor();

        // Bottom-anchored popup: the normal UI (status + conversation) is
        // drawn first and the panel covers only the bottom rows, so the
        // conversation stays visible above it.
        // Wrap the sanitized question to the popup width.
        std::string qsan = sanitize_text(question);
        if (qsan.empty()) qsan = "(no question)";
        std::vector<std::string> qlines;
        for (const auto& raw : util::split_lines(qsan)) {
            std::string rest = raw;
            if (rest.empty()) {
                qlines.push_back("");
                continue;
            }
            while (utf8_len(rest) > static_cast<size_t>(cols)) {
                qlines.push_back(utf8_prefix(rest, static_cast<size_t>(cols)));
                rest = rest.substr(qlines.back().size());
            }
            qlines.push_back(rest);
        }
        if (qlines.empty()) qlines.push_back(qsan);

        // Panel chrome rows: top rule + blank + blank + hint + bottom rule,
        // plus the custom input row while editing. The panel never grows
        // past max_popup rows so the conversation stays visible above it.
        int max_popup = rows - 2;  // keep status + at least one convo row
        if (max_popup < 6) max_popup = 6;
        int chrome = 5 + (editing ? 1 : 0);
        size_t max_q = static_cast<size_t>(std::max(1, max_popup - chrome - 1));
        if (max_q > 4) max_q = 4;  // a popup is not the place for essays
        if (qlines.size() > max_q) {
            qlines.resize(max_q);
            qlines.back() = utf8_prefix(qlines.back(), static_cast<size_t>(std::max(0, cols - 3))) + "...";
        }

        int vis_count = max_popup - chrome - static_cast<int>(qlines.size());
        if (vis_count < 1) vis_count = 1;
        size_t vis = std::min(opts.size(), static_cast<size_t>(vis_count));
        size_t start = 0;
        if (opts.size() > vis) {
            size_t half = vis / 2;
            start = sel > half ? sel - half : 0;
            if (start + vis > opts.size()) start = opts.size() - vis;
        }

        // Draw the normal UI first; the popup then covers only its bottom.
        draw_status();
        draw_viewport();

        int total = 1 + static_cast<int>(qlines.size()) + 1 + static_cast<int>(vis) +
                    1 + (editing ? 1 : 0) + 1 + 1;
        int r = rows - total + 1;
        if (r < 2) r = 2;  // never cover the status row
        term_.move_cursor(r, 1);
        term_.clear_line();
        std::string top = "[ ask ]-";
        if (top.size() < static_cast<size_t>(cols))
            top += std::string(static_cast<size_t>(cols) - top.size(), '-');
        write_styled(term_, STYLE_DIM, utf8_prefix(top, static_cast<size_t>(cols)));
        ++r;
        for (const auto& ql : qlines) {
            term_.move_cursor(r, 1);
            term_.clear_line();
            write_styled(term_, STYLE_BOLD | STYLE_YELLOW, ql);
            ++r;
        }
        ++r;  // blank line
        for (size_t k = 0; k < vis; ++k) {
            size_t idx = start + k;
            if (r > rows) break;
            term_.move_cursor(r, 1);
            term_.clear_line();
            bool is_sel = (idx == sel);
            std::string prefix = is_sel ? "> " : "  ";
            std::string text = flatten(opts[idx]);
            size_t avail = static_cast<size_t>(std::max(4, cols - 4));
            if (utf8_len(text) > avail)
                text = utf8_prefix(text, avail > 3 ? avail - 3 : 0) + "...";
            int style = is_sel ? (STYLE_REVERSE | STYLE_BOLD) : STYLE_NORMAL;
            write_styled(term_, style, prefix + text);
            ++r;
        }
        ++r;  // blank line
        int input_row = -1;
        std::string input_prompt = "Type your answer: ";
        size_t input_cursor_col = 0;
        if (editing) {
            if (r <= rows) {
                term_.move_cursor(r, 1);
                term_.clear_line();
                write_styled(term_, STYLE_BOLD | STYLE_GREEN, input_prompt);
                int avail = cols - static_cast<int>(input_prompt.size());
                if (avail < 4) avail = 4;
                auto chars = split_chars(custom);
                size_t cursor_char = char_count_before(custom, custom_cursor);
                size_t win_start = 0;
                if (chars.size() > static_cast<size_t>(avail)) {
                    win_start = cursor_char >= static_cast<size_t>(avail)
                        ? cursor_char - static_cast<size_t>(avail) + 1
                        : 0;
                    if (win_start + static_cast<size_t>(avail) > chars.size())
                        win_start = chars.size() - static_cast<size_t>(avail);
                }
                std::string input_shown;
                for (size_t k = win_start;
                     k < chars.size() && k < win_start + static_cast<size_t>(avail); ++k)
                    input_shown += chars[k];
                term_.write(input_shown);
                input_cursor_col = cursor_char >= win_start ? cursor_char - win_start : 0;
                input_row = r;
                ++r;
            }
        }
        if (r <= rows) {
            term_.move_cursor(r, 1);
            term_.clear_line();
            std::string hint;
            if (editing) {
                hint = "Enter to confirm, Esc to go back";
            } else {
                hint = "Up/Down to select, Enter to confirm";
                if (opts.size() > vis)
                    hint += " (" + std::to_string(sel + 1) + "/" + std::to_string(opts.size()) + ")";
            }
            write_styled(term_, STYLE_DIM, utf8_prefix(hint, static_cast<size_t>(cols)));
            ++r;
        }
        if (r > rows) r = rows;
        term_.move_cursor(r, 1);
        term_.clear_line();
        write_styled(term_, STYLE_DIM, std::string(static_cast<size_t>(cols), '-'));
        if (editing && input_row > 0) {
            term_.move_cursor(input_row,
                              static_cast<int>(input_prompt.size() + input_cursor_col) + 1);
            term_.show_cursor();
        }
    };

    auto restore = [&]() {
        input_ = saved_input;
        cursor_ = std::min(saved_cursor, input_.size());
        redraw();
    };

    auto custom_insert = [&](uint32_t cp) {
        std::string u = utf8_from_cp(cp);
        custom.insert(custom_cursor, u);
        custom_cursor += u.size();
    };
    auto custom_backspace = [&]() {
        if (custom_cursor == 0) return;
        size_t i = custom_cursor;
        do { --i; } while (i > 0 && (static_cast<unsigned char>(custom[i]) & 0xC0) == 0x80);
        custom.erase(i, custom_cursor - i);
        custom_cursor = i;
    };
    auto custom_delete = [&]() {
        if (custom_cursor >= custom.size()) return;
        unsigned char c = static_cast<unsigned char>(custom[custom_cursor]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (custom_cursor + len > custom.size()) len = custom.size() - custom_cursor;
        custom.erase(custom_cursor, len);
    };
    auto custom_left = [&]() {
        if (custom_cursor == 0) return;
        do { --custom_cursor; } while (custom_cursor > 0 &&
            (static_cast<unsigned char>(custom[custom_cursor]) & 0xC0) == 0x80);
    };
    auto custom_right = [&]() {
        if (custom_cursor >= custom.size()) return;
        unsigned char c = static_cast<unsigned char>(custom[custom_cursor]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        custom_cursor = std::min(custom.size(), custom_cursor + len);
    };

    for (;;) {
        draw();
        if (Terminal::got_sigterm.load()) {
            restore();
            return std::nullopt;
        }
        auto ev = in.read(100);
        if (!ev) continue;
        if (Terminal::got_sigterm.load()) {
            restore();
            return std::nullopt;
        }
        if (editing) {
            switch (ev->key) {
                case Key::Enter: {
                    std::string ans = util::trim(custom);
                    if (ans.empty()) {
                        term_.write("\a");
                        break;
                    }
                    restore();
                    return ans;
                }
                case Key::Escape:
                    editing = false;
                    break;
                case Key::CtrlC:
                case Key::CtrlQ:
                    restore();
                    return std::nullopt;
                case Key::Char:
                    if (ev->codepoint >= 32) custom_insert(ev->codepoint);
                    break;
                case Key::Backspace: custom_backspace(); break;
                case Key::Delete: custom_delete(); break;
                case Key::Left: custom_left(); break;
                case Key::Right: custom_right(); break;
                case Key::Home: custom_cursor = 0; break;
                case Key::End: custom_cursor = custom.size(); break;
                case Key::Up:
                case Key::Down:
                    break;  // selection is keyboard-driven outside the editor
                default:
                    break;
            }
            continue;
        }
        switch (ev->key) {
            case Key::Up:
                sel = (sel + opts.size() - 1) % opts.size();
                break;
            case Key::Down:
                sel = (sel + 1) % opts.size();
                break;
            case Key::Enter:
                if (sel == custom_idx) {
                    editing = true;
                    custom.clear();
                    custom_cursor = 0;
                } else {
                    std::string ans = opts[sel];
                    restore();
                    return ans;
                }
                break;
            case Key::Escape:
            case Key::CtrlC:
            case Key::CtrlQ:
                restore();
                return std::nullopt;
            default:
                break;  // typing does nothing in the list; Enter the custom row to type
        }
    }
}

void UI::message(const std::string& msg, int style) {
    append(msg, style);
}

void UI::set_working(bool w) {
    bool was = working_depth_ > 0;
    working_depth_ += w ? 1 : -1;
    if (working_depth_ < 0) working_depth_ = 0;
    if (was != (working_depth_ > 0)) redraw();  // spinner appears / clears
}

void UI::tick_spinner() {
    if (working_depth_ <= 0) return;
    uint64_t now = util::steady_ms();
    if (now - spinner_last_ms_ < 300) return;
    spinner_last_ms_ = now;
    spinner_frame_ = (spinner_frame_ + 1) % 4;
    redraw();
}
