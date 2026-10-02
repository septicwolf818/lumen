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

    // Ensure directories exist
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

    // Make sure we restore even on unexpected exit paths
    // (destructor of Terminal also restores)

    InputReader input;
    UI ui(term);
    Agent agent(term, ui, input);

    int rc = 0;
    try {
        rc = agent.run();
    } catch (const std::exception& e) {
        term.restore();
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        rc = 1;
    }

    term.restore();
    // Leave a clean line
    std::fputs("\n", stdout);
    std::fflush(stdout);
    return rc;
}
