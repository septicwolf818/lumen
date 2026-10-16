#include "tools.hpp"
#include "util.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <algorithm>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace {

// Ceiling on a per-call timeout, so a model cannot ask execute() to hang for
// hours; the config default is the usual budget and this only bounds overrides.
constexpr int64_t kMaxCommandTimeoutSecs = 3600;

// Upper bound on one poll() slice. The wait also ends on child exit, so this
// only decides how coarsely a still-running child is re-checked, not how long
// the caller waits past the command finishing.
constexpr int kPollSliceMs = 50;

// Reason a file path cannot be opened; "" means "exists, not a directory"
// (the caller's open() decides the rest).
std::string file_reason(const std::string& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (st.type() == fs::file_type::not_found || ec == std::errc::no_such_file_or_directory)
        return "file does not exist: " + p;
    if (ec == std::errc::permission_denied) return "permission denied: " + p;
    if (ec) return "cannot access " + p + ": " + ec.message();
    if (st.type() == fs::file_type::directory)
        return "\"" + p + "\" is a directory - use list_directory instead";
    return "";
}

// Reason a directory cannot be listed; "" means "is a directory".
std::string dir_reason(const std::string& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (st.type() == fs::file_type::not_found || ec == std::errc::no_such_file_or_directory)
        return "directory does not exist: " + p;
    if (ec == std::errc::permission_denied) return "permission denied: " + p;
    if (ec) return "cannot access " + p + ": " + ec.message();
    if (st.type() != fs::file_type::directory) return "\"" + p + "\" is a file, not a directory";
    if (::access(p.c_str(), R_OK | X_OK) != 0)
        return std::string("permission denied: ") + p;
    return "";
}

std::string os_error(const char* verb, const std::string& p) {
    int e = errno;
    return std::string(verb) + " " + p + ": " + (e ? std::strerror(e) : "unknown error");
}

}  // namespace

nlohmann::json tool_definitions() {
    return nlohmann::json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "read_file"},
                {"description", "Read a text file. Path is relative to the working directory; include subdirectories, e.g. src/main.cpp. Optional start (1-based line) and limit (max lines) read only part of the file; the output then starts with a header showing the line range. On failure the error names the path that was tried."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type", "string"}, {"description", "File path"}}},
                        {"start", {{"type", "integer"}, {"description", "1-based line number to start from (default 1)"}}},
                        {"limit", {{"type", "integer"}, {"description", "Maximum lines to return from start (default: to end of file)"}}}
                    }},
                    {"required", nlohmann::json::array({"path"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "write_file"},
                {"description", "Write or replace the entire contents of a file. Creates parent directories if needed."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type", "string"}}},
                        {"content", {{"type", "string"}}}
                    }},
                    {"required", nlohmann::json::array({"path", "content"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "edit_file"},
                {"description", "Replace a specific substring in a file with new text. old_text must match exactly once."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type", "string"}}},
                        {"old_text", {{"type", "string"}}},
                        {"new_text", {{"type", "string"}}}
                    }},
                    {"required", nlohmann::json::array({"path", "old_text", "new_text"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "list_directory"},
                {"description", "List files and directories at path. Output is prefixed with the path so entries can be passed to other tools directly."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {{"type", "string"}, {"description", "Directory path (default .)"}}}
                    }},
                    {"required", nlohmann::json::array()}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "execute"},
                {"description", "Run a shell command in the working directory. Prefer non-interactive commands. A command still running when its timeout expires is killed."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"command", {{"type", "string"}}},
                        {"timeout_seconds", {{"type", "integer"},
                                             {"description", "Kill the command after this many seconds (default 120, max 3600). Raise it for slow builds or test suites."}}}
                    }},
                    {"required", nlohmann::json::array({"command"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "search"},
                {"description", "Search for a text pattern recursively under path using ripgrep (or grep). Each match is one line of path:line:text, with path relative to the working directory. No matches is a normal result, not an error."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"pattern", {{"type", "string"}}},
                        {"path", {{"type", "string"}, {"description", "Root path (default .)"}}}
                    }},
                    {"required", nlohmann::json::array({"pattern"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "ask"},
                {"description", "Ask the user a question with selectable options. Use when you need clarification, a decision, or input before continuing. Provide a concise question and 2-4 short options; a \"Type your own answer...\" option is added automatically and lets the user type a custom answer. Returns the user's selected or typed answer as text."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"question", {{"type", "string"}, {"description", "Question to show the user"}}},
                        {"options", {{"type", "array"}, {"description", "Answer options to choose from (2-4 short labels)"}, {"items", {{"type", "string"}}}, {"minItems", 1}}}
                    }},
                    {"required", nlohmann::json::array({"question", "options"})}
                }}
            }}
        }
    });
}

ToolRunner::ToolRunner(Config& cfg, std::string cwd) : cfg_(cfg), cwd_(std::move(cwd)) {}

bool ToolRunner::is_readonly(const std::string& name) {
    // ask is read-only in the sense that it changes nothing on disk, so it
    // never needs the y/N destructive-tool prompt; it runs its own
    // interactive picker instead.
    return name == "read_file" || name == "list_directory" || name == "search" ||
           name == "ask";
}

std::optional<std::string> ToolRunner::safe_path(const std::string& user_path) {
    auto p = util::resolve_safe_path(cwd_, user_path, cfg_.allow_path_escape);
    if (!p) return std::nullopt;
    return p->string();
}

std::string ToolRunner::path_denied(const std::string& user_path) const {
    return "path \"" + user_path + "\" resolves outside the working directory (" + cwd_ +
           ") - path escape is disabled";
}

ToolResult ToolRunner::run(const std::string& name,
                           const nlohmann::json& arguments,
                           std::function<bool(const std::string& prompt)> confirm_fn,
                           std::function<bool()> pump_fn) {
    // Validate before prompting: confirming a nonexistent tool would waste a
    // prompt on a call that cannot run.
    bool known = name == "read_file" || name == "write_file" || name == "edit_file" ||
                 name == "list_directory" || name == "execute" || name == "search" ||
                 name == "ask";
    if (!known) {
        return {false, "unknown tool \"" + name +
                           "\" - available: read_file, write_file, edit_file, "
                           "list_directory, execute, search, ask"};
    }

    // ask runs its own interactive picker; it never takes the y/N prompt.
    // A cancellation stays a plain "Cancelled by user." like the y/N path,
    // so it renders as a normal result rather than a red error.
    if (name == "ask") {
        auto r = ask(arguments);
        r.output = util::sanitize_utf8(r.output);
        if (!r.ok && r.output != "Cancelled by user." &&
            r.output.rfind("ERROR:", 0) != 0)
            r.output = "ERROR: " + r.output;
        return r;
    }

    bool readonly = is_readonly(name);
    if (!readonly && !cfg_.auto_approve_tools) {
    // The caller already prints the tool name and arguments, so the prompt is
    // just the question.
        std::string prompt = "Execute " + name + "? [y/N] ";
        if (!confirm_fn || !confirm_fn(prompt)) {
            return {false, "Cancelled by user."};
        }
    }

    ToolResult r;
    if (name == "read_file") r = read_file(arguments);
    else if (name == "write_file") r = write_file(arguments);
    else if (name == "edit_file") r = edit_file(arguments);
    else if (name == "list_directory") r = list_directory(arguments);
    else if (name == "execute") r = execute(arguments, pump_fn);
    else r = search(arguments, pump_fn);

    // execute/search can emit arbitrary bytes: always sanitize first.
    r.output = util::sanitize_utf8(r.output);
    if (r.output.size() > cfg_.max_tool_output_bytes) {
        r.output = util::truncate(r.output, cfg_.max_tool_output_bytes);
        r.output = util::sanitize_utf8(r.output);
    }
    if (!r.ok && r.output.rfind("ERROR:", 0) != 0) r.output = "ERROR: " + r.output;
    return r;
}

ToolResult ToolRunner::read_file(const nlohmann::json& args) {
    if (!args.contains("path")) return {false, "missing required argument \"path\""};
    if (!args["path"].is_string()) return {false, "argument \"path\" must be a string"};
    std::string arg = args["path"].get<std::string>();
    auto sp = safe_path(arg);
    if (!sp) return {false, path_denied(arg)};

    std::string why = file_reason(*sp);
    if (!why.empty()) return {false, why};

    bool paging = false;
    long start = 1;
    long limit = 0;  // 0 = to end of file
    if (args.contains("start")) {
        if (!args["start"].is_number_integer() || args["start"].get<long>() < 1)
            return {false, "argument \"start\" must be a positive integer"};
        start = args["start"].get<long>();
        paging = true;
    }
    if (args.contains("limit")) {
        if (!args["limit"].is_number_integer() || args["limit"].get<long>() < 1)
            return {false, "argument \"limit\" must be a positive integer"};
        limit = args["limit"].get<long>();
        paging = true;
    }

    errno = 0;
    std::ifstream in(*sp, std::ios::binary);
    if (!in) return {false, os_error("cannot open", *sp)};

    if (paging) {
        // Stream with getline and stop at the limit so paging huge files is
        // cheap.
        char sample[8192];
        in.read(sample, sizeof(sample));
        std::string head(sample, static_cast<size_t>(in.gcount()));
        if (util::looks_binary(head)) {
            return {false,
                    "refusing to read binary/non-text file: " + *sp +
                        ". Use execute with hexdump/od/file if you need binary inspection."};
        }
        in.clear();
        in.seekg(0);

        std::vector<std::string> lines;
        std::string line;
        long lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            if (lineno < start) continue;
            lines.push_back(line);
            if (limit > 0 && static_cast<long>(lines.size()) >= limit) break;
        }
        if (lines.empty()) {
            if (lineno == 0) return {false, "file is empty: " + *sp};
            return {false, "start line " + std::to_string(start) + " is past the end of " +
                               *sp + " (" + std::to_string(lineno) +
                               (lineno == 1 ? " line)" : " lines)")};
        }

        long total = -1;  // known only if we hit EOF
        if (static_cast<long>(lines.size()) < limit || limit == 0) {
            total = lineno;  // getline ran out
        } else {
            std::string peek;
            if (!std::getline(in, peek)) total = lineno;
        }

        long last = start + static_cast<long>(lines.size()) - 1;
        std::string header = "[" + *sp + " lines " + std::to_string(start) + "-" +
                             std::to_string(last);
        if (total >= 0) header += " of " + std::to_string(total);
        else header += "; more lines below - raise start/limit to continue";
        header += "]";

        std::string text;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) text += "\n";
            text += lines[i];
        }
        if (text.size() > cfg_.max_file_bytes)
            text = util::truncate(text, cfg_.max_file_bytes);
        text = util::sanitize_utf8(header + "\n" + text);
        return {true, text};
    }

    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    if (sz < 0) return {false, "cannot size file"};

    size_t to_read = static_cast<size_t>(sz);
    bool truncated = false;
    if (to_read > cfg_.max_file_bytes) {
        to_read = cfg_.max_file_bytes;
        truncated = true;
    }

    in.seekg(0);
    std::string buf(to_read, '\0');
    in.read(&buf[0], static_cast<std::streamsize>(to_read));
    buf.resize(static_cast<size_t>(in.gcount()));

    if (util::looks_binary(buf)) {
        return {false,
                "refusing to read binary/non-text file: " + *sp +
                    " (" + std::to_string(static_cast<size_t>(sz)) + " bytes). "
                    "Use execute with hexdump/od/file if you need binary inspection."};
    }

    std::string text = util::sanitize_utf8(buf);  // valid UTF-8 for JSON/Ollama
    if (truncated) {
        text += "\n[output truncated at " + std::to_string(cfg_.max_file_bytes) + " bytes]\n";
    }
    if (!util::is_valid_utf8(text)) text = util::sanitize_utf8(text);  // belt-and-braces
    return {true, text};
}

ToolResult ToolRunner::write_file(const nlohmann::json& args) {
    if (!args.contains("path")) return {false, "missing required argument \"path\""};
    if (!args["path"].is_string()) return {false, "argument \"path\" must be a string"};
    if (!args.contains("content")) return {false, "missing required argument \"content\""};
    if (!args["content"].is_string()) return {false, "argument \"content\" must be a string"};
    std::string arg = args["path"].get<std::string>();
    auto sp = safe_path(arg);
    if (!sp) return {false, path_denied(arg)};

    std::error_code ec;
    if (fs::is_directory(*sp, ec))
        return {false, "\"" + *sp + "\" is a directory - cannot write a file over it"};

    fs::path p(*sp);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec)
            return {false, "cannot create parent directories for " + *sp + ": " + ec.message()};
    }

    errno = 0;
    std::ofstream out(*sp, std::ios::binary | std::ios::trunc);
    if (!out) return {false, os_error("cannot write", *sp)};
    out << args["content"].get<std::string>();
    if (!out) return {false, os_error("cannot write", *sp)};
    return {true, "Wrote " + *sp + " (" + std::to_string(args["content"].get<std::string>().size()) + " bytes)"};
}

ToolResult ToolRunner::edit_file(const nlohmann::json& args) {
    if (!args.contains("path")) return {false, "missing required argument \"path\""};
    if (!args["path"].is_string()) return {false, "argument \"path\" must be a string"};
    if (!args.contains("old_text")) return {false, "missing required argument \"old_text\""};
    if (!args["old_text"].is_string()) return {false, "argument \"old_text\" must be a string"};
    if (!args.contains("new_text")) return {false, "missing required argument \"new_text\""};
    if (!args["new_text"].is_string()) return {false, "argument \"new_text\" must be a string"};
    std::string arg = args["path"].get<std::string>();
    auto sp = safe_path(arg);
    if (!sp) return {false, path_denied(arg)};

    std::string why = file_reason(*sp);
    if (!why.empty()) return {false, why};

    errno = 0;
    std::ifstream in(*sp, std::ios::binary);
    if (!in) return {false, os_error("cannot open", *sp)};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    std::string old_t = args["old_text"].get<std::string>();
    std::string new_t = args["new_text"].get<std::string>();
    if (old_t.empty())
        return {false, "argument \"old_text\" must not be empty"};

    size_t pos = content.find(old_t);
    if (pos == std::string::npos)
        return {false, "old_text not found in " + *sp};
    size_t matches = 1;
    for (size_t p = content.find(old_t, pos + old_t.size()); p != std::string::npos;
         p = content.find(old_t, p + old_t.size())) {
        ++matches;
    }
    if (matches > 1)
        return {false, "old_text matches " + std::to_string(matches) + " times in " + *sp +
                           " - add more surrounding context so it matches exactly once"};

    content.replace(pos, old_t.size(), new_t);
    errno = 0;
    std::ofstream out(*sp, std::ios::binary | std::ios::trunc);
    if (!out) return {false, os_error("cannot write", *sp)};
    out << content;
    if (!out) return {false, os_error("cannot write", *sp)};
    return {true, "Edited " + *sp};
}

ToolResult ToolRunner::list_directory(const nlohmann::json& args) {
    std::string path = ".";
    if (args.contains("path")) {
        if (!args["path"].is_string())
            return {false, "argument \"path\" must be a string"};
        path = args["path"].get<std::string>();
    }
    auto sp = safe_path(path);
    if (!sp) return {false, path_denied(path)};

    std::string why = dir_reason(*sp);
    if (!why.empty()) return {false, why};

    // Prefix entries with the requested path so results can be passed straight
    // to read_file/edit_file.
    std::string prefix;
    if (path != "." && path != "./" && path != "/") {
        prefix = path;
        if (prefix.back() != '/') prefix += '/';
    }

    std::error_code ec;
    std::ostringstream out;
    out << "Listing " << path << ":\n";
    std::vector<std::string> entries;
    // Manual iteration with ec: the throwing overloads would terminate on a
    // stat error mid-listing.
    fs::directory_iterator it(*sp, ec);
    if (ec) return {false, "cannot list " + *sp + ": " + ec.message()};
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::string name = it->path().filename().string();
        std::error_code tec;
        if (it->is_directory(tec)) name += "/";
        entries.push_back(prefix + name);
    }
    if (ec) return {false, "cannot list " + *sp + ": " + ec.message()};
    std::sort(entries.begin(), entries.end());
    for (auto& e : entries) out << e << "\n";
    if (entries.empty()) out << "(empty)\n";
    return {true, out.str()};
}

ToolResult ToolRunner::execute(const nlohmann::json& args,
                              const std::function<bool()>& pump) {
    if (!args.contains("command")) return {false, "missing required argument \"command\""};
    if (!args["command"].is_string()) return {false, "argument \"command\" must be a string"};
    std::string cmd = args["command"].get<std::string>();

    // Wall-clock budget. A per-call timeout_seconds wins over the config
    // default; 0 in the config means no limit. It is a ceiling, never a delay:
    // the wait below ends the moment the command is done, however much of the
    // budget is left.
    uint64_t budget_ms = 0;
    int64_t secs = cfg_.command_timeout;
    if (args.contains("timeout_seconds")) {
        if (!args["timeout_seconds"].is_number_integer())
            return {false, "argument \"timeout_seconds\" must be an integer"};
        secs = args["timeout_seconds"].get<int64_t>();
    }
    if (secs > 0)
        budget_ms = static_cast<uint64_t>(std::min(secs, kMaxCommandTimeoutSecs)) * 1000;

    // /bin/sh -c in cwd, stdout+stderr captured, output capped.
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return {false, std::string("pipe failed: ") + std::strerror(errno)};

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        return {false, std::string("fork failed: ") + std::strerror(e)};
    }
    if (pid == 0) {
        // child: async-signal-safe calls only from here on
        // Own process group, so the timeout can kill the whole tree. Killing
        // only the shell would leave a backgrounded grandchild running.
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (chdir(cwd_.c_str()) != 0) {
            static const char m[] = "chdir failed: cannot change to the working directory\n";
            ssize_t w = ::write(STDOUT_FILENO, m, sizeof(m) - 1); (void)w;
            _exit(127);
        }
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        static const char m[] = "exec failed: cannot start /bin/sh\n";
        ssize_t w = ::write(STDOUT_FILENO, m, sizeof(m) - 1); (void)w;
        _exit(127);
    }
    // Also set it here: whichever of parent and child gets there first, the
    // group exists before the timeout can try to signal it. EACCES just means
    // the child already exec'd, which is fine.
    setpgid(pid, pid);
    close(pipefd[1]);

    std::string output;
    char buf[4096];
    size_t limit = cfg_.max_tool_output_bytes;
    const uint64_t start = util::steady_ms();
    bool timed_out = false;
    bool truncated = false;
    bool cancelled = false;
    bool reaped = false;
    int status = 0;

    // Watch the pipe and the child together. poll() returns on whichever
    // happens first, so a command that finishes early ends the wait at once
    // instead of idling out the rest of its timeout.
    for (;;) {
        // Let the caller keep its UI alive while the command runs, and let it
        // call the wait off. Skipped once the child is reaped: that tail is a
        // pure drain of the pipe buffer, not a wait on anything.
        if (pump && !reaped && pump()) {
            cancelled = true;
            break;
        }
        int slice = kPollSliceMs;
        if (budget_ms) {
            uint64_t elapsed = util::steady_ms() - start;
            if (elapsed >= budget_ms) {
                timed_out = true;
                break;
            }
            slice = static_cast<int>(std::min<uint64_t>(budget_ms - elapsed,
                                                        static_cast<uint64_t>(kPollSliceMs)));
        }
        // Once the child is reaped, only what is already buffered can arrive:
        // poll non-blocking so the tail of its output is picked up without
        // waiting on a write end a surviving grandchild may still hold open.
        if (reaped) slice = 0;

        struct pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        int pr = ::poll(&pfd, 1, slice);
        if (pr < 0) {
            // Signals lack SA_RESTART, so resize/Ctrl-C fails poll with EINTR -
            // not EOF, so retry rather than cut output short.
            if (errno == EINTR) continue;
            break;
        }
        // Child already reaped and nothing left buffered: its output is
        // complete, so stop rather than spin on a zero-timeout poll.
        if (reaped && (pr == 0 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR)))) break;
        if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                size_t take = std::min(static_cast<size_t>(n), limit - output.size());
                output.append(buf, take);
                if (output.size() >= limit) {
                    truncated = true;
                    break;
                }
            } else if (n == 0) {
                break;  // EOF: the command closed its output
            } else if (errno != EINTR && errno != EAGAIN) {
                break;
            }
        }
        // The child can exit while a grandchild still holds the write end, so
        // EOF alone is not "finished". Reaping here ends the wait at the real
        // end of the command rather than at the end of the timeout.
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            reaped = true;
        } else if (w < 0 && errno != EINTR) {
            break;
        }
    }

    if (timed_out || cancelled) killpg(pid, SIGKILL);  // the group, not just the shell
    close(pipefd[0]);
    if (!reaped) {
        // Retry until the child is reaped: giving up would leave a zombie and
        // let status 0 misreport a failure as success.
        pid_t w;
        while ((w = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
        }
        reaped = (w == pid);
    }
    if (!reaped) {
        // status was never filled in; reporting it would claim success.
        return {false, std::string("waitpid failed: ") + std::strerror(errno)};
    }
    if (cancelled) {
        // Whatever the command printed before it was killed is still its output;
        // the exit status belongs to our SIGKILL, not to the command.
        if (output.empty()) output = "(no output)";
        return {false, output + "\n[cancelled by user - command killed]\n"};
    }
    if (timed_out) {
        if (output.empty()) output = "(no output)";
        output += "\n[timed out after " + std::to_string(budget_ms / 1000) +
                  "s - command killed]\n";
        return {false, output};
    }
    // Hitting the limit closes the pipe under the child (SIGPIPE, reported as
    // 141), so that exit status is our doing, not a failure.
    if (truncated) output += "\n[output truncated]\n";
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (WIFSIGNALED(status)) {
        if (!(truncated && WTERMSIG(status) == SIGPIPE))
            output += "\n[killed by signal " + std::to_string(WTERMSIG(status)) + "]\n";
    } else if (code != 0 && !truncated) {
        output += "\n[exit code " + std::to_string(code) + "]\n";
    }
    if (output.empty()) output = "(no output)";
    return {code == 0 || truncated, output};
}

ToolResult ToolRunner::search(const nlohmann::json& args,
                             const std::function<bool()>& pump) {
    if (!args.contains("pattern")) return {false, "missing required argument \"pattern\""};
    if (!args["pattern"].is_string()) return {false, "argument \"pattern\" must be a string"};
    std::string pattern = args["pattern"].get<std::string>();
    if (pattern.empty()) return {false, "argument \"pattern\" must not be empty"};
    std::string path = ".";
    if (args.contains("path")) {
        if (!args["path"].is_string())
            return {false, "argument \"path\" must be a string"};
        path = args["path"].get<std::string>();
    }
    auto sp = safe_path(path);
    if (!sp) return {false, path_denied(path)};
    std::error_code ec;
    if (!fs::exists(*sp, ec) || ec)
        return {false, "search path does not exist: " + *sp};

    // Prefer rg, fall back to grep -r (probed at run time; rg is not always in
    // /usr/bin). Exit code 1 = "no matches", a normal result, normalized to 0.
    std::string root = *sp;
    std::error_code rec;
    fs::path rel = fs::relative(*sp, fs::weakly_canonical(cwd_), rec);
    if (!rec) {
        std::string rs = rel.string();
        if (rs.empty()) {
            root = ".";
        } else if (rs.rfind("..", 0) != 0) {
            root = rs;  // stays under cwd, so matches read back as relative paths
        }
    }
    std::string pat = util::shell_escape(pattern);
    std::string rt = util::shell_escape(root);
    std::string cmd = "if command -v rg >/dev/null 2>&1; then "
                      "rg -n --no-heading --with-filename -S --max-count 50 " + pat + " " + rt +
                      " || [ $? -eq 1 ]; else "
                      "grep -rnH --max-count=50 -E " + pat + " " + rt +
                      " || [ $? -eq 1 ]; fi";
    nlohmann::json a;  // reuse the execute path (no extra confirmation)
    a["command"] = cmd;
    auto r = execute(a, pump);
    if (r.ok && r.output == "(no output)")
        r.output = "no matches for " + util::shell_escape(pattern) + " in " + root;
    return r;
}

ToolResult ToolRunner::ask(const nlohmann::json& args) {
    if (!args.contains("question")) return {false, "missing required argument \"question\""};
    if (!args["question"].is_string()) return {false, "argument \"question\" must be a string"};
    std::string question = util::trim(util::sanitize_utf8(args["question"].get<std::string>()));
    if (question.empty()) return {false, "argument \"question\" must not be empty"};
    if (question.size() > 2000) question = util::trim(question.substr(0, 2000));

    if (!args.contains("options")) return {false, "missing required argument \"options\""};
    if (!args["options"].is_array()) return {false, "argument \"options\" must be an array of strings"};
    if (args["options"].empty()) return {false, "argument \"options\" must not be empty"};
    if (args["options"].size() > 10)
        return {false, "argument \"options\" must have at most 10 items"};
    std::vector<std::string> options;
    for (const auto& o : args["options"]) {
        if (!o.is_string()) return {false, "argument \"options\" must be an array of strings"};
        std::string s = util::trim(util::sanitize_utf8(o.get<std::string>()));
        if (s.empty()) return {false, "argument \"options\" must not contain empty strings"};
        if (s.size() > 500) s = util::trim(s.substr(0, 500));
        options.push_back(std::move(s));
    }

    if (!ask_fn_) return {false, "ask tool has no interactive handler in this context"};
    auto answer = ask_fn_(question, options);
    if (!answer) return {false, "Cancelled by user."};
    std::string a = util::trim(util::sanitize_utf8(*answer));
    if (a.empty()) return {false, "Cancelled by user."};
    return {true, a};
}
