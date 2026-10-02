#pragma once

#include <termios.h>
#include <sys/ioctl.h>
#include <atomic>
#include <functional>
#include <string>

// Raw TTY control for Linux virtual consoles (TERM=linux) and SSH.
// Restores settings on exit / signal.

class Terminal {
public:
    Terminal();
    ~Terminal();

    // Enter raw mode (non-canonical, no echo)
    bool enter_raw();
    // Restore original settings
    void restore();

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    void update_size();  // call on SIGWINCH

    // ANSI helpers (VT100-safe)
    void clear_screen();
    void clear_line();
    void move_cursor(int row, int col);  // 1-based
    void hide_cursor();
    void show_cursor();
    void set_scroll_region(int top, int bottom);  // 1-based inclusive
    void reset_scroll_region();

    // Write to stdout (handles partial writes)
    void write(const std::string& s);
    void write(const char* s, size_t n);
    void flush();

    // Basic colour (optional, works on most TTYs)
    void set_fg(int ansi_color);  // 0-7 or 30-37 style
    void reset_attrs();

    bool is_raw() const { return raw_; }

    // Signal-safe flag: set by signal handlers
    static std::atomic<bool> got_sigint;
    static std::atomic<bool> got_sigterm;
    static std::atomic<bool> got_sigwinch;
    static std::atomic<int>  sigint_count;

    static void install_signal_handlers();

private:
    struct termios orig_{};
    bool raw_ = false;
    bool has_orig_ = false;
    int rows_ = 24;
    int cols_ = 80;
};
