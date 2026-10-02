#pragma once

#include "terminal.hpp"
#include "input.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <deque>

// Simple scrollback + status + input line UI.
// Layout:
//   row 1: status (model / session / cwd)
//   rows 2..H-2: conversation viewport
//   row H-1: separator / scroll indicator
//   row H: input line

// Line styles for scrollback: a bitmask so a line can carry bold/dim plus one
// colour. Deliberately limited to the SGR attributes we emit - bold, dim and
// the basic foreground colours 31-36 (black and white are never needed) -
// all rendered by the Linux console (TERM=linux), so no 256-colour (38;5;n)
// or truecolor (38;2;r;g;b) sequences are emitted.
// STYLE_DIM == 1, so append(text, 1) callers keep their old meaning.
enum UiStyle : int {
    STYLE_NORMAL  = 0,
    STYLE_DIM     = 1 << 0,
    STYLE_BOLD    = 1 << 1,
    STYLE_RED     = 1 << 2,
    STYLE_GREEN   = 1 << 3,
    STYLE_YELLOW  = 1 << 4,
    STYLE_BLUE    = 1 << 5,
    STYLE_MAGENTA = 1 << 6,
    STYLE_CYAN    = 1 << 7,
};

struct DisplayLine {
    std::string text;
    int style = 0;  // UiStyle bitmask
};

class UI {
public:
    explicit UI(Terminal& term);

    void set_status(const std::string& model,
                    const std::string& session,
                    const std::string& cwd);

    // Context window segment of the status line, e.g. "12.4k/32k (39%)"
    void set_context(const std::string& ctx) { status_ctx_ = ctx; }

    // Append a line (or multi-line) to scrollback. Auto-scrolls if at bottom.
    void append(const std::string& text, int style = 0);
    void append_raw(const std::string& text);  // no wrap, as-is

    // Streaming: append tokens to the last "open" assistant line.
    // style applies to the whole streamed block (STYLE_DIM for thinking).
    void begin_stream(int style = 0);
    void stream_token(const std::string& tok);
    void end_stream();

    void clear_conversation();

    // Scroll
    void scroll_up(int n = 1);
    void scroll_down(int n = 1);
    void scroll_page_up();
    void scroll_page_down();
    void scroll_home();
    void scroll_end();
    bool at_bottom() const;

    // Input line editing
    std::string& input_buf() { return input_; }
    size_t& cursor() { return cursor_; }
    void set_input(const std::string& s);
    void set_input(const std::string& s, size_t cursor);
    void input_insert(uint32_t cp);
    void input_backspace();
    void input_delete();
    void input_left();
    void input_right();
    void input_home();
    void input_end();
    void input_history_up();
    void input_history_down();
    void push_history(const std::string& s);

    // Full redraw
    void redraw();
    void on_resize();

    // Exit path: reprint the conversation that is on screen (plain Linux
    // console sequences) and print the farewell line for the shell.
    void dump_screen();
    void farewell();

    // Prompt for y/N confirmation (blocks on input)
    bool confirm(InputReader& in, const std::string& prompt);

    // Append a styled line to the scrollback (e.g. a transient notice)
    void message(const std::string& msg, int style = STYLE_DIM);

    // Queued messages waiting for the current response to finish
    void set_queue(size_t n) { queue_count_ = n; }
    size_t queue() const { return queue_count_; }

    // Working indicator: RAII-style depth counter, so nested generation /
    // tool / compaction frames keep the spinner up until the outermost one
    // finishes. The separator row shows a |/-\ frame instead of "---".
    void set_working(bool w);
    bool working() const { return working_depth_ > 0; }
    void tick_spinner();  // advance the frame (throttled) and redraw
    void tick_marquee();

    int viewport_rows() const;

private:
    Terminal& term_;
    std::deque<DisplayLine> lines_;
    size_t scroll_offset_ = 0;  // lines from bottom (0 = bottom)
    std::string status_model_;
    std::string status_session_;
    std::string status_ctx_;
    std::string status_cwd_;
    std::string input_;
    size_t cursor_ = 0;
    std::vector<std::string> history_;
    int history_idx_ = -1;
    std::string history_stash_;
    bool streaming_ = false;
    int stream_style_ = 0;
    std::string stream_acc_;
    size_t stream_base_ = 0;  // index of the line begin_stream() opened
    std::string last_message_;
    size_t queue_count_ = 0;
    int working_depth_ = 0;      // nested set_working() frames
    int spinner_frame_ = 0;      // index into "|/-\"
    uint64_t spinner_last_ms_ = 0;
    size_t marquee_offset_ = 0;
    uint64_t marquee_last_ms_ = 0;

    void wrap_and_push(const std::string& text, int style);
    std::vector<std::string> wrap(const std::string& text, int width) const;
    void viewport_window(int& start, int& rows) const;
    std::vector<std::pair<std::string, int>> status_parts() const;
    void draw_status();
    void draw_viewport();
    void draw_input();
    std::string utf8_from_cp(uint32_t cp);
};
