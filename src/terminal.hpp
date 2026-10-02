#pragma once

#include <termios.h>
#include <sys/ioctl.h>
#include <atomic>
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

    // Alternate screen buffer (DECSET/DECRST 1049): the session is drawn on
    // the alt buffer so leave_alt_screen() can restore the console content
    // that was on screen before lumen started. Plain sequences only - what a
    // Linux VT understands; older kernels ignore them and the UI's exit dump
    // rebuilds the screen instead.
    void enter_alt_screen();
    void leave_alt_screen();

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    void update_size();  // call on SIGWINCH

    // ANSI helpers (VT100-safe)
    void clear_screen();
    void clear_line();
    void move_cursor(int row, int col);  // 1-based
    void hide_cursor();
    void show_cursor();

    // Write to stdout (handles partial writes)
    void write(const std::string& s);
    void write(const char* s, size_t n);

    void reset_attrs();

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
    bool alt_ = false;
    int rows_ = 24;
    int cols_ = 80;
};
