#include "terminal.hpp"
#include "util.hpp"
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>

std::atomic<bool> Terminal::got_sigint{false};
std::atomic<bool> Terminal::got_sigterm{false};
std::atomic<bool> Terminal::got_sigwinch{false};
std::atomic<int>  Terminal::sigint_count{0};

static Terminal* g_term = nullptr;

static void on_signal(int sig) {
    if (sig == SIGINT) {
        Terminal::got_sigint.store(true);
        Terminal::sigint_count.fetch_add(1);
    } else if (sig == SIGTERM) {
        Terminal::got_sigterm.store(true);
    } else if (sig == SIGWINCH) {
        Terminal::got_sigwinch.store(true);
    }
}

Terminal::Terminal() {
    g_term = this;
    update_size();
}

Terminal::~Terminal() {
    restore();
    if (g_term == this) g_term = nullptr;
}

void Terminal::install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // do not restart syscalls – we want EINTR

    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGWINCH, &sa, nullptr);

    // Ignore SIGTSTP so we don't suspend into a bad state
    signal(SIGTSTP, SIG_IGN);
}

bool Terminal::enter_raw() {
    if (!isatty(STDIN_FILENO)) return false;
    if (tcgetattr(STDIN_FILENO, &orig_) != 0) return false;
    has_orig_ = true;

    struct termios raw = orig_;
    // Non-canonical, no echo, no signal chars (we handle Ctrl-C ourselves)
    raw.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | INLCR | IGNCR | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 0;   // non-blocking read
    raw.c_cc[VTIME] = 1;   // 100ms timeout

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    raw_ = true;
    return true;
}

void Terminal::restore() {
    if (has_orig_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
        has_orig_ = false;
    }
    if (raw_) {
        // restore cursor and attrs (no attr reset under NO_COLOR: nothing
        // styling-related was written in the first place)
        const char* seq = util::no_color() ? "\033[?25h\033[r"
                                           : "\033[0m\033[?25h\033[r";
        ::write(STDOUT_FILENO, seq, std::strlen(seq));
        raw_ = false;
    }
}

void Terminal::update_size() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows_ = ws.ws_row;
        cols_ = ws.ws_col;
    } else {
        // fallback from env
        const char* r = std::getenv("LINES");
        const char* c = std::getenv("COLUMNS");
        if (r) rows_ = std::atoi(r);
        if (c) cols_ = std::atoi(c);
        if (rows_ < 1) rows_ = 24;
        if (cols_ < 1) cols_ = 80;
    }
}

void Terminal::write(const std::string& s) {
    write(s.data(), s.size());
}

void Terminal::write(const char* s, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(STDOUT_FILENO, s + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(w);
    }
}

void Terminal::flush() {
    // stdout is unbuffered after we write directly; nothing extra needed
}

void Terminal::clear_screen() {
    write("\033[H\033[2J");
}

void Terminal::clear_line() {
    write("\033[2K");
}

void Terminal::move_cursor(int row, int col) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
    if (n > 0) write(buf, static_cast<size_t>(n));
}

void Terminal::hide_cursor() {
    write("\033[?25l");
}

void Terminal::show_cursor() {
    write("\033[?25h");
}

void Terminal::set_scroll_region(int top, int bottom) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "\033[%d;%dr", top, bottom);
    if (n > 0) write(buf, static_cast<size_t>(n));
}

void Terminal::reset_scroll_region() {
    write("\033[r");
}

void Terminal::set_fg(int color) {
    if (util::no_color()) return;  // NO_COLOR: emit no styling at all
    // color 0-7 -> 30-37
    if (color >= 0 && color <= 7) {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "\033[%dm", 30 + color);
        if (n > 0) write(buf, static_cast<size_t>(n));
    } else if (color >= 30 && color <= 37) {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "\033[%dm", color);
        if (n > 0) write(buf, static_cast<size_t>(n));
    }
}

void Terminal::reset_attrs() {
    write("\033[0m");
}
