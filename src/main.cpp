#include "terminal.hpp"
#include "input.hpp"
#include "ui.hpp"
#include "agent.hpp"
#include "util.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static void cleanup_curl() {
    curl_global_cleanup();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    util::ensure_dir(util::config_dir());
    util::ensure_dir(util::sessions_dir());

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        std::fprintf(stderr, "curl_global_init failed\n");
        return 1;
    }
    atexit(cleanup_curl);

    Terminal term;
    Terminal::install_signal_handlers();

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::fprintf(stderr, "lumen requires a TTY (stdin/stdout).\n");
        std::fprintf(stderr, "Try running on /dev/tty1 or in a terminal emulator.\n");
        return 1;
    }

    if (!term.enter_raw()) {
        std::fprintf(stderr, "Failed to set terminal to raw mode.\n");
        return 1;
    }

    // Draw on the alt screen so the prior console content is restored on exit
    // (ignored where unsupported; the exit dump rebuilds the screen instead).
    // Terminal's destructor also restores, covering unexpected exit paths.
    term.enter_alt_screen();

    InputReader input;
    UI ui(term);
    Agent agent(term, ui, input);

    // Exit path: cooked mode first (so \n works), then the screen dump (the
    // fallback when alt screen was ignored), leave the alt screen, farewell.
    auto finish = [&]() {
        term.restore();
        ui.dump_screen();
        term.leave_alt_screen();
        ui.farewell();
        std::fflush(stdout);
    };

    int rc = 0;
    const char* fatal = nullptr;
    try {
        rc = agent.run();
    } catch (const std::exception& e) {
        fatal = e.what();
        rc = 1;
    }

    finish();
    if (fatal) std::fprintf(stderr, "Fatal: %s\n", fatal);
    return rc;
}
