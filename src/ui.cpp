#include "ui.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstring>

namespace {

// SGR sequences for UiStyle. Only bold/dim and basic colours 31-36 (black and
// white are never used), which the Linux console renders; keeps every line
// compatible with TERM=linux.
// Returns an empty string under NO_COLOR, so write_styled emits plain text.
std::string sgr(int style) {
    if (util::no_color()) return "";
    std::string s;
    if (style & STYLE_BOLD) s += "\033[1m";
    if (style & STYLE_DIM) s += "\033[2m";
    if (style & STYLE_RED) s += "\033[31m";
    if (style & STYLE_GREEN) s += "\033[32m";
    if (style & STYLE_YELLOW) s += "\033[33m";
    if (style & STYLE_BLUE) s += "\033[34m";
    if (style & STYLE_MAGENTA) s += "\033[35m";
    if (style & STYLE_CYAN) s += "\033[36m";
    return s;
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

}  // namespace

UI::UI(Terminal& term) : term_(term) {}

void UI::set_status(const std::string& model, const std::string& session, const std::string& cwd) {
    status_model_ = model;
    status_session_ = session;
    status_cwd_ = cwd;
}

int UI::viewport_rows() const {
    // 1 status + 1 sep + 1 input = 3 reserved
    return std::max(1, term_.rows() - 3);
}

void UI::wrap_and_push(const std::string& text, int style) {
    auto wrapped = wrap(text, std::max(10, term_.cols()));
    for (auto& w : wrapped) {
        lines_.push_back({w, style});
    }
    while (lines_.size() > kMaxLines) {
        lines_.pop_front();
        if (stream_base_ > 0) --stream_base_;  // indices shift with the pop
    }
}

std::vector<std::string> UI::wrap(const std::string& text, int width) const {
    std::vector<std::string> out;
    if (text.empty()) {
        out.push_back("");
        return out;
    }
    size_t i = 0;
    while (i < text.size()) {
        // Build one line, counting display columns. Control characters must
        // never reach the terminal raw: a tab would expand past the wrapped
        // width and cut the line on the right, and a stray ESC/CR would move
        // the cursor mid-line and visibly truncate it.
        std::string line;
        size_t cols = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == '\n') {
                ++i;
                break;
            }
            if (c == '\t') {
                size_t pad = 4 - (cols % 4);  // expand to the next tab stop
                if (cols + pad > static_cast<size_t>(width)) break;
                line.append(pad, ' ');
                cols += pad;
                ++i;
                continue;
            }
            if (c < 0x20 || c == 0x7F) {  // other controls: never shown
                ++i;
                continue;
            }
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len > text.size()) len = 1;
            if (cols >= static_cast<size_t>(width)) break;
            line.append(text, i, len);
            cols += 1;
            i += len;
        }
        out.push_back(std::move(line));
    }
    if (out.empty()) out.push_back("");
    return out;
}

void UI::append(const std::string& text, int style) {
    // split on newlines first
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

void UI::append_raw(const std::string& text) {
    append(text, 0);
}

void UI::begin_stream(int style) {
    streaming_ = true;
    stream_style_ = style;
    stream_acc_.clear();
    lines_.push_back({"", style});
    // The stream block owns everything from this line on; stream_token()
    // pops back to here before re-wrapping the accumulated text.
    stream_base_ = lines_.size() - 1;
}

void UI::stream_token(const std::string& tok) {
    if (!streaming_) {
        begin_stream();
    }
    stream_acc_ += tok;
    auto wrapped = wrap(stream_acc_, std::max(10, term_.cols()));
    while (lines_.size() > stream_base_) lines_.pop_back();
    for (auto& w : wrapped) lines_.push_back({w, stream_style_});
    if (lines_.empty()) lines_.push_back({"", stream_style_});
}

void UI::end_stream() {
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
void UI::scroll_home() {
    int max_off = static_cast<int>(lines_.size()) - viewport_rows();
    scroll_offset_ = max_off > 0 ? static_cast<size_t>(max_off) : 0;
}
void UI::scroll_end() { scroll_offset_ = 0; }
bool UI::at_bottom() const { return scroll_offset_ == 0; }

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

void UI::draw_status() {
    term_.move_cursor(1, 1);
    term_.clear_line();

    // Assemble as (text, style) parts so the visible text can be truncated to
    // the terminal width without cutting into an escape sequence.
    struct Part {
        std::string text;
        int style;
    };
    std::vector<Part> parts;
    auto kv = [&parts](const std::string& label, const std::string& value) {
        if (!parts.empty()) parts.push_back({"  ", STYLE_NORMAL});
        parts.push_back({label, STYLE_BOLD | STYLE_CYAN});
        parts.push_back({value, STYLE_NORMAL});
    };
    kv("model:", status_model_.empty() ? "(none)" : status_model_);
    kv("session:", status_session_.empty() ? "(none)" : status_session_);
    if (!status_ctx_.empty()) kv("ctx:", status_ctx_);
    if (!status_cwd_.empty()) kv("cwd:", status_cwd_);

    int avail = term_.cols();
    for (auto& p : parts) {
        if (avail <= 0) break;
        // Truncate by characters (utf8_prefix) and count the written part in
        // characters too - byte sizes would cut multi-byte values early.
        p.text = utf8_prefix(p.text, static_cast<size_t>(avail));
        write_styled(term_, p.style, p.text);
        avail -= static_cast<int>(utf8_len(p.text));
    }
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
            // Character-based cut: wrap() already limits lines to cols
            // characters, so this is a no-op unless the window shrank. A
            // byte-based cut here would chop characters that actually fit.
            std::string t = utf8_prefix(dl.text, static_cast<size_t>(term_.cols()));
            write_styled(term_, dl.style, t);  // escapes are added after truncation
        }
    }

    // separator / scroll / spinner indicator
    term_.move_cursor(term_.rows() - 1, 1);
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
    // show input, handle overflow simply: show tail around cursor
    int avail = term_.cols() - static_cast<int>(prefix.size());
    if (avail < 4) avail = 4;
    std::string shown = input_;
    size_t vis_cursor = cursor_;
    if (static_cast<int>(shown.size()) > avail) {
        // window around cursor
        size_t start = 0;
        if (cursor_ + 1 > static_cast<size_t>(avail))
            start = cursor_ + 1 - static_cast<size_t>(avail);
        shown = input_.substr(start, static_cast<size_t>(avail));
        vis_cursor = cursor_ - start;
    }
    term_.write(shown);
    // place cursor
    term_.move_cursor(term_.rows(), static_cast<int>(prefix.size()) + static_cast<int>(vis_cursor) + 1);
}

void UI::redraw() {
    term_.hide_cursor();
    draw_status();
    draw_viewport();
    draw_input();
    term_.show_cursor();
    // cursor already placed in draw_input
}

// Reprint the conversation window sequentially so it stays readable after
// the app exits: inside the alt screen it is discarded with the buffer; on a
// terminal that ignored the alt screen it rebuilds a clean last frame (the
// status / separator / input rows are dropped). Only sequences the plain
// Linux console supports are used (home, erase display, SGR).
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
        // Re-wrap to the current width: lines were wrapped when appended and
        // the window may have shrunk since (on_resize does not re-wrap).
        for (const auto& w : wrap(dl.text, width)) {
            write_styled(term_, dl.style, w);
            term_.write("\r\n");
        }
    }
    // Cursor left at column 1 of the row after the last printed line.
}

void UI::farewell() {
    // Leading newline: after leave_alt_screen() the cursor sits right behind
    // the old shell prompt, so the message must start on its own row.
    term_.write("\r\n");
    write_styled(term_, STYLE_BOLD | STYLE_GREEN, "lumen: Bye!");
    term_.write(" Session was ");
    write_styled(term_, STYLE_BOLD,
                 status_session_.empty() ? "(none)" : status_session_);
    term_.write("\r\n");
}

void UI::on_resize() {
    term_.update_size();
    // re-wrap would be ideal; for simplicity just redraw
    redraw();
}

bool UI::confirm(InputReader& in, const std::string& prompt) {
    // Raw mode has OPOST off, so a '\n' would not return to column 1 and a
    // multi-line prompt would staircase across the screen. Put everything
    // except the last line into the scrollback and keep the question on the
    // input row.
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
        if (ev->key == Key::Enter) return false;  // default N
        if (ev->key == Key::CtrlC || ev->key == Key::Escape) return false;
        if (ev->key == Key::CtrlQ) return false;  // must never be swallowed
    }
}

void UI::message(const std::string& msg, int style) {
    last_message_ = msg;
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
