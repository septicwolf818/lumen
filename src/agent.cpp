#include "agent.hpp"
#include "util.hpp"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unistd.h>

namespace {
// Marker stored with a compaction summary so replay can render it distinctly.
constexpr const char* kSummaryTag = "[conversation summary]";
}  // namespace

Agent::Agent(Terminal& term, UI& ui, InputReader& input)
    : term_(term), ui_(ui), input_(input) {
    cfg_.load();
    ollama_ = std::make_unique<OllamaClient>(cfg_.ollama_url);
    char cwd[4096];
    std::string cwd_str = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
    tools_ = std::make_unique<ToolRunner>(cfg_, cwd_str);
}

std::string Agent::system_prompt() const {
    return
        "You are a coding assistant running in a minimal Linux terminal agent.\n"
        "You can use tools to read/write files, list directories, search, and execute shell commands.\n"
        "Working directory: " + tools_->cwd() + "\n"
        "Prefer small, focused tool calls. After tools return, explain results clearly.\n"
        "When editing code, use edit_file with exact old_text matches when possible.\n"
        "Do not invent file contents; read them first.";
}

void Agent::init_session() {
    if (!cfg_.last_session.empty()) {
        auto s = Session::load(cfg_.last_session);
        if (s) {
            session_ = *s;
            if (!session_.model.empty()) cfg_.model = session_.model;
            if (!session_.cwd.empty()) {
                tools_->set_cwd(session_.cwd);
                chdir(session_.cwd.c_str());
            }
            // replay messages into UI
            for (const auto& m : session_.messages) replay_message(m);
            update_status();
            return;
        }
    }
    new_session("default");
}

void Agent::update_status() {
    refresh_ctx_limit();
    ui_.set_status(cfg_.model, session_.name, tools_->cwd());
    update_ctx_display();
}

size_t Agent::sent_chars() const {
    size_t n = 0;
    for (const auto& m : session_.messages) {
        auto j = m.to_json();
        j.erase("thinking");  // display-only: never sent back to the model
        n += j.dump().size();
    }
    return n;
}

void Agent::refresh_ctx_limit() {
    if (ctx_model_ != cfg_.model) {
        ctx_model_ = cfg_.model;
        ctx_limit_ = 0;
        ctx_anchor_ = -1;
        ctx_anchor_chars_ = 0;
    }
    if (ctx_limit_ > 0 || cfg_.model.empty()) return;
    if (cfg_.context_limit > 0) {
        ctx_limit_ = static_cast<int>(cfg_.context_limit);
        return;
    }
    ctx_limit_ = ollama_->context_length(cfg_.model);
}

void Agent::reset_ctx_tracking() {
    ctx_model_.clear();
    ctx_limit_ = 0;
    ctx_anchor_ = -1;
    ctx_anchor_chars_ = 0;
}

// Tokens the next request would use: exact at the anchor (Ollama-reported),
// estimated at ~4 chars/token for everything added since.
int Agent::estimate_ctx_used() const {
    size_t chars = sent_chars();
    if (ctx_anchor_ >= 0 && chars >= ctx_anchor_chars_)
        return ctx_anchor_ + static_cast<int>((chars - ctx_anchor_chars_) / 4);
    return static_cast<int>(chars / 4);
}

void Agent::update_ctx_display() {
    size_t chars = sent_chars();
    int used;
    bool exact = false;
    if (ctx_anchor_ >= 0 && chars >= ctx_anchor_chars_) {
        used = ctx_anchor_ + static_cast<int>((chars - ctx_anchor_chars_) / 4);
        exact = (chars == ctx_anchor_chars_);
    } else {
        used = static_cast<int>(chars / 4);
    }
    std::string s = (exact ? "" : "~") + util::fmt_tokens(static_cast<size_t>(used));
    if (ctx_limit_ > 0) {
        s += "/" + util::fmt_tokens(static_cast<size_t>(ctx_limit_));
        s += " (" + std::to_string(used * 100 / ctx_limit_) + "%)";
    }
    ui_.set_context(s);
}

void Agent::new_session(const std::string& name) {
    session_ = Session{};
    session_.id = Session::make_id();
    session_.name = name.empty() ? "session-" + session_.id.substr(2, 6) : name;
    session_.model = cfg_.model;
    session_.created = util::now_iso8601();
    session_.modified = session_.created;
    session_.cwd = tools_->cwd();
    session_.messages.clear();
    Message sys;
    sys.role = Role::System;
    sys.content = system_prompt();
    session_.messages.push_back(sys);
    ui_.clear_conversation();
    ui_.append("New session: " + session_.name, STYLE_BOLD);
    ui_.append("");
    cfg_.last_session = session_.name;
    cfg_.save();
    update_status();
}

void Agent::save_session(bool notify) {
    session_.model = cfg_.model;
    session_.modified = util::now_iso8601();
    session_.cwd = tools_->cwd();
    if (session_.save()) {
        cfg_.last_session = session_.name;
        cfg_.save();
        if (notify) ui_.message("Session saved: " + session_.name);
    } else {
        ui_.message("Failed to save session", STYLE_RED);
    }
}

void Agent::load_session(const std::string& name) {
    auto s = Session::load(name);
    if (!s) {
        ui_.message("Session not found: " + name, STYLE_RED);
        return;
    }
    session_ = *s;
    if (!session_.model.empty()) cfg_.model = session_.model;
    if (!session_.cwd.empty()) {
        tools_->set_cwd(session_.cwd);
        chdir(session_.cwd.c_str());
    }
    cfg_.last_session = session_.name;
    cfg_.save();
    ui_.clear_conversation();
    ui_.append("Loaded session: " + session_.name, STYLE_BOLD);
    for (const auto& m : session_.messages) replay_message(m);
    ui_.append("");
    update_status();
}

// Draw one stored message the same way on startup and on /session load,
// including tool calls and their results.
void Agent::replay_message(const Message& m) {
    if (m.role == Role::System) return;
    if (m.role == Role::User) {
        if (m.content.rfind(kSummaryTag, 0) == 0) {
            // compaction summary: render as its own block, not a user prompt
            ui_.append("[conversation summary]", 1);
            std::string rest = util::trim(m.content.substr(std::strlen(kSummaryTag)));
            if (!rest.empty()) ui_.append(rest);
            ui_.append("");
        } else {
            ui_.append("You:", STYLE_BOLD);
            ui_.append(m.content);
            ui_.append("");
        }
    } else if (m.role == Role::Assistant) {
        ui_.append("Agent:", STYLE_BOLD | STYLE_CYAN);
        bool has_content = !m.content.empty();
        bool has_tools = m.tool_calls.is_array() && !m.tool_calls.empty();
        if (!m.thinking.empty()) {
            // Thinking models: reasoning rendered dim, exactly as it streamed.
            ui_.append(m.thinking, STYLE_DIM);
            if (has_content || has_tools) ui_.append("");
        }
        if (has_content) ui_.append(m.content);
        if (m.tool_calls.is_array()) {
            for (const auto& tc : m.tool_calls) {
                std::string name = "tool";
                if (tc.contains("function") && tc["function"].contains("name"))
                    name = tc["function"]["name"].get<std::string>();
                ui_.append("[tool] " + name, STYLE_YELLOW);
            }
        }
        ui_.append("");
    } else if (m.role == Role::Tool) {
        ui_.append("[result]", 1);
        std::string c = m.content;
        if (c.size() > 500) c = c.substr(0, 500) + "\n[truncated]";
        ui_.append(c, c.rfind("ERROR:", 0) == 0 ? STYLE_RED : STYLE_NORMAL);
        ui_.append("");
    }
}

std::vector<nlohmann::json> Agent::build_messages() const {
    std::vector<nlohmann::json> out;
    for (const auto& m : session_.messages) {
        auto j = m.to_json();
        // Saved for display on session reopen, but not resent: the model
        // re-reasons each turn and thinking would only bloat the context.
        j.erase("thinking");
        out.push_back(std::move(j));
    }
    return out;
}

void Agent::show_help() {
    ui_.append("Commands:", STYLE_BOLD);
    ui_.append("/help                 Show this help");
    ui_.append("/model [NAME]         List or select model");
    ui_.append("/session              Show current session");
    ui_.append("/session new [NAME]   New session");
    ui_.append("/session list         List sessions");
    ui_.append("/session load NAME    Load session");
    ui_.append("/session rename A B   Rename");
    ui_.append("/session delete NAME  Delete");
    ui_.append("/cd PATH              Change directory");
    ui_.append("/pwd                  Print directory");
    ui_.append("/clear                Clear display");
    ui_.append("/compact              Compact conversation");
    ui_.append("/tools                List tools");
    ui_.append("/config               Show config");
    ui_.append("/quit                 Exit");
    ui_.append("");
    ui_.append("Keys: Ctrl-C interrupt, Ctrl-L model, Ctrl-N new, Ctrl-S save, Ctrl-Q quit");
    ui_.append("      PageUp/Down scroll, Up/Down history");
    ui_.append("");
}

bool Agent::select_model_interactive() {
    std::string err;
    auto models = ollama_->list_models(&err);
    if (!models) {
        ui_.message("Error: " + err, STYLE_RED);
        return false;
    }
    if (models->empty()) {
        ui_.message("No models found. Pull one with: ollama pull <name>", STYLE_RED);
        return false;
    }
        ui_.append("Available models:", STYLE_BOLD);
    for (size_t i = 0; i < models->size(); ++i) {
        ui_.append(std::to_string(i + 1) + ". " + (*models)[i].name);
    }
    ui_.append("Select number or name (empty cancels):");
    ui_.redraw();

    // simple line read
    std::string line;
    ui_.set_input("");
    for (;;) {
        ui_.redraw();
        auto ev = input_.read(100);
        if (Terminal::got_sigterm.load()) return false;
        if (!ev) continue;
        if (ev->key == Key::Enter) {
            line = ui_.input_buf();
            break;
        }
        if (ev->key == Key::CtrlC || ev->key == Key::Escape) {
            ui_.set_input("");
            return false;
        }
        if (ev->key == Key::Char) ui_.input_insert(ev->codepoint);
        if (ev->key == Key::Backspace) ui_.input_backspace();
        if (ev->key == Key::Left) ui_.input_left();
        if (ev->key == Key::Right) ui_.input_right();
    }
    ui_.set_input("");
    line = util::trim(line);
    if (line.empty()) return false;

    // number?
    try {
        size_t idx = static_cast<size_t>(std::stoul(line));
        if (idx >= 1 && idx <= models->size()) {
            cfg_.model = (*models)[idx - 1].name;
            session_.model = cfg_.model;
            cfg_.save();
            update_status();
            ui_.message("Model: " + cfg_.model);
            return true;
        }
    } catch (...) {}

    // name match
    for (auto& m : *models) {
        if (m.name == line || m.name.find(line) != std::string::npos) {
            cfg_.model = m.name;
            session_.model = cfg_.model;
            cfg_.save();
            update_status();
            ui_.message("Model: " + cfg_.model);
            return true;
        }
    }
    ui_.message("Unknown model: " + line, STYLE_RED);
    return false;
}

void Agent::handle_command(const std::string& line) {
    auto parts = util::split_lines(line);  // not ideal
    // simple split on space
    std::vector<std::string> tok;
    {
        std::istringstream iss(line);
        std::string t;
        while (iss >> t) tok.push_back(t);
    }
    if (tok.empty()) return;
    std::string cmd = tok[0];
    for (auto& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (cmd == "/help" || cmd == "/?") {
        show_help();
    } else if (cmd == "/quit" || cmd == "/exit") {
        running_ = false;
    } else if (cmd == "/model") {
        if (tok.size() >= 2) {
            cfg_.model = tok[1];
            session_.model = cfg_.model;
            cfg_.save();
            update_status();
            ui_.message("Model: " + cfg_.model);
        } else {
            select_model_interactive();
        }
    } else if (cmd == "/session") {
        if (tok.size() == 1) {
            ui_.append("Session: " + session_.name + " id=" + session_.id, STYLE_BOLD);
            ui_.append("Model: " + session_.model);
            ui_.append("Messages: " + std::to_string(session_.messages.size()));
            ui_.append("cwd: " + session_.cwd);
        } else if (tok[1] == "new") {
            std::string n = tok.size() >= 3 ? tok[2] : "";
            new_session(n);
        } else if (tok[1] == "list") {
            auto names = Session::list();
            ui_.append("Sessions:", STYLE_BOLD);
            if (names.empty()) ui_.append("(none)");
            for (auto& n : names) ui_.append("  " + n);
        } else if (tok[1] == "load" && tok.size() >= 3) {
            load_session(tok[2]);
        } else if (tok[1] == "delete" && tok.size() >= 3) {
            if (Session::remove(tok[2])) ui_.message("Deleted " + tok[2]);
            else ui_.message("Not found", STYLE_RED);
        } else if (tok[1] == "rename" && tok.size() >= 4) {
            session_.name = tok[3];
            save_session();
        } else {
            ui_.message("Usage: /session [new|list|load|delete|rename] ...", STYLE_YELLOW);
        }
    } else if (cmd == "/cd") {
        if (tok.size() < 2) {
            ui_.message("Usage: /cd PATH", STYLE_YELLOW);
            return;
        }
        std::string path = line.substr(line.find_first_not_of(' ', 3));
        path = util::trim(path);
        auto sp = util::resolve_safe_path(tools_->cwd(), path, true);
        if (!sp || !fs::is_directory(*sp)) {
            ui_.message("Cannot cd: " + path, STYLE_RED);
            return;
        }
        if (chdir(sp->c_str()) != 0) {
            ui_.message("chdir failed", STYLE_RED);
            return;
        }
        tools_->set_cwd(sp->string());
        session_.cwd = sp->string();
        update_status();
        ui_.message("cwd: " + sp->string());
    } else if (cmd == "/pwd") {
        ui_.message(tools_->cwd());
    } else if (cmd == "/clear") {
        ui_.clear_conversation();
    } else if (cmd == "/compact") {
        do_compact();
    } else if (cmd == "/tools") {
        ui_.append("Tools: read_file, write_file, edit_file, list_directory, execute, search", STYLE_BOLD);
        ui_.append("auto_approve_tools=" + std::string(cfg_.auto_approve_tools ? "true" : "false"));
    } else if (cmd == "/config") {
        ui_.append(cfg_.to_json().dump(2));
    } else if (cmd == "/reload") {
        cfg_.load();
        ollama_->set_base_url(cfg_.ollama_url);
        update_status();
        ui_.message("Config reloaded");
    } else {
        ui_.message("Unknown command: " + cmd + " (try /help)", STYLE_RED);
    }
}

namespace {

// Replace input[start, end) with repl and put the cursor after it.
void replace_token(UI& ui, size_t start, size_t end, const std::string& repl) {
    std::string in = ui.input_buf();
    if (start > in.size()) return;
    if (end > in.size()) end = in.size();
    ui.set_input(in.substr(0, start) + repl + in.substr(end), start + repl.size());
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

}  // namespace

// Tab completion for slash commands: first press extends to the longest
// common prefix and lists the candidates, further presses cycle through them.
void Agent::handle_tab() {
    std::string in = ui_.input_buf();
    auto t = completion::analyze(in, ui_.cursor());
    if (!t.valid) return;
    if (t.matches.empty()) {
        term_.write("\a");  // no match: bell
        return;
    }
    std::string word = in.substr(t.start, t.end - t.start);

    // Keep cycling while the token is still the one we completed.
    if (tab_cycle_.active && tab_cycle_.start == t.start &&
        (word == tab_cycle_.token || contains(tab_cycle_.matches, word))) {
        if (tab_cycle_.started) {
            tab_cycle_.idx = (tab_cycle_.idx + 1) % tab_cycle_.matches.size();
        } else {
            tab_cycle_.started = true;
            tab_cycle_.idx = 0;
        }
        replace_token(ui_, t.start, t.end, tab_cycle_.matches[tab_cycle_.idx]);
        return;
    }

    // Fresh completion.
    if (t.matches.size() == 1) {
        tab_cycle_ = TabCycle{};
        bool at_end = (t.end == in.size());
        // Directory completions end in '/' so the next segment can follow;
        // everything else terminates the token with a space.
        bool opens_dir = t.matches[0].back() == '/';
        replace_token(ui_, t.start, t.end, t.matches[0] + (at_end && !opens_dir ? " " : ""));
        return;
    }

    std::string common = completion::common_prefix(t.matches);
    tab_cycle_.active = true;
    tab_cycle_.start = t.start;
    // Path candidates can share no prefix ("/cd /home/" -> bin, etc, ...):
    // anchor cycling on the typed word instead of an empty common prefix,
    // and never replace the token with an empty string.
    tab_cycle_.token = common.empty() ? word : common;
    tab_cycle_.matches = t.matches;
    tab_cycle_.idx = 0;
    tab_cycle_.started = false;
    if (!common.empty()) replace_token(ui_, t.start, t.end, common);

    std::string list;
    for (size_t i = 0; i < t.matches.size(); ++i) {
        if (i) list += "  ";
        list += t.matches[i];
    }
    ui_.append(list, 1);
}

void Agent::do_compact(bool automatic) {
    if (cfg_.model.empty()) {
        ui_.message("Select a model first (/model)", STYLE_YELLOW);
        return;
    }
    if (session_.messages.size() <= 2) {
        ui_.message("Nothing to compact");
        return;
    }

    auto role_name = [](Role r) -> const char* {
        switch (r) {
            case Role::User: return "User";
            case Role::Assistant: return "Assistant";
            case Role::Tool: return "Tool";
            default: return "System";
        }
    };

    size_t before = session_.messages.size();
    std::string tag = automatic ? "[auto-compact]" : "[compact]";
    ui_.append("");
    ui_.append(tag + " summarizing " + std::to_string(before) + " messages...", 1);
    ui_.redraw();

    std::ostringstream prompt;
    prompt
        << "You are compacting a coding-assistant conversation into a handoff "
           "summary. The assistant will continue from your summary instead of "
           "the original history, so preserve everything needed to keep "
           "working:\n"
        << "- the user's goal, constraints, and current task state\n"
        << "- decisions made and the reasons behind them\n"
        << "- file paths, commands, identifiers, and error messages verbatim\n"
        << "- what was tried, what worked, and what failed\n"
        << "- pending work and the concrete next step\n"
        << "Be terse and factual; do not restate pleasantries. Output only the "
           "summary.\n\n";
    for (const auto& m : session_.messages) {
        if (m.role == Role::System) continue;
        std::string c = m.content;
        if (c.size() > 2000) c = c.substr(0, 2000) + "\n[...truncated]";
        prompt << role_name(m.role) << ": " << c << "\n";
        if (m.tool_calls.is_array() && !m.tool_calls.empty())
            prompt << "Assistant tool calls: " << m.tool_calls.dump() << "\n";
    }

    // Stream the summary into the transcript so the user sees what will be
    // kept; cancellable with Ctrl+C like any other generation.
    abort_gen_ = false;
    cancel_all_ = false;
    Terminal::got_sigint.store(false);
    Terminal::sigint_count.store(0);
    auto should_abort = [this]() -> bool {
        if (Terminal::got_sigterm.load()) return true;
        if (Terminal::got_sigint.load()) {
            abort_gen_ = true;
            Terminal::got_sigint.store(false);
        }
        pump_input();
        return abort_gen_.load();
    };

    std::vector<nlohmann::json> msgs = {
        {{"role", "user"}, {"content", prompt.str()}}};
    nlohmann::json final_msg;
    std::string err;
    // Same two-phase display as run_generation: thinking models open a dim
    // block first, plain models stream the summary in the normal style.
    int phase = 0;
    bool ok = ollama_->chat_stream(
        cfg_.model, msgs, nlohmann::json::array(),
        [this, &phase](const std::string& tok) {
            if (phase == 1) {  // thinking block open: close it, then summary
                ui_.end_stream();
                ui_.append("");
                ui_.begin_stream();
            } else if (phase == 0) {
                ui_.begin_stream();
            }
            phase = 2;
            ui_.stream_token(tok);
            static uint64_t last = 0;
            uint64_t now = util::steady_ms();
            if (now - last > 80) {
                ui_.redraw();
                last = now;
            }
        },
        [this, &phase](const std::string& tok) {
            if (phase == 2) return;  // summary already started
            if (phase == 0) ui_.begin_stream(STYLE_DIM);
            phase = 1;
            ui_.stream_token(tok);
            static uint64_t last = 0;
            uint64_t now = util::steady_ms();
            if (now - last > 80) {
                ui_.redraw();
                last = now;
            }
        },
        nullptr, should_abort, &err, &final_msg);
    ui_.end_stream();
    ui_.append("");

    if (err == "interrupted" || abort_gen_) {
        ui_.message(tag + " cancelled - conversation unchanged", STYLE_YELLOW);
        abort_gen_ = false;
        cancel_all_ = false;
        ui_.redraw();
    } else if (!ok) {
        ui_.message("Compaction failed: " + err, STYLE_RED);
        ui_.redraw();
    } else {
        std::string sum = final_msg.contains("content") && final_msg["content"].is_string()
                              ? final_msg["content"].get<std::string>()
                              : "";
        sum = util::trim(sum);
        if (sum.empty()) {
            ui_.message("Compaction failed: empty summary", STYLE_RED);
            ui_.redraw();
        } else {
            Message sys;
            if (!session_.messages.empty() && session_.messages[0].role == Role::System) {
                sys = session_.messages[0];
            } else {
                sys.role = Role::System;
                sys.content = system_prompt();
            }

            std::vector<Message> kept;
            kept.push_back(sys);
            Message summary;
            summary.role = Role::User;
            summary.content = std::string(kSummaryTag) + "\n" + sum;
            kept.push_back(summary);

            size_t from = session_.messages.size() > 4 ? session_.messages.size() - 4 : 1;
            // Never start the kept tail on a tool result: pull back to include
            // the assistant message that requested it, so pairs stay intact.
            while (from > 1 && session_.messages[from].role == Role::Tool) --from;
            for (size_t i = from; i < session_.messages.size(); ++i) {
                if (session_.messages[i].role != Role::System)
                    kept.push_back(session_.messages[i]);
            }
            // A trailing assistant message that only carries tool calls has no
            // results yet (unfinished turn) - drop it.
            while (kept.size() > 2 && kept.back().role == Role::Assistant &&
                   kept.back().tool_calls.is_array() && !kept.back().tool_calls.empty()) {
                kept.pop_back();
            }
            session_.messages = kept;

            // The history changed completely: anchor is invalid, but the
            // window limit for this model is still valid.
            ctx_anchor_ = -1;
            ctx_anchor_chars_ = 0;
            update_ctx_display();
            ui_.message(tag + " done: " + std::to_string(before) + " -> " +
                        std::to_string(kept.size()) + " messages");
            save_session(false);
        }
    }

    // A slash command typed while compacting runs now (like after a turn).
    if (!pending_cmd_.empty()) {
        std::string cmd = std::move(pending_cmd_);
        pending_cmd_.clear();
        cancel_all_ = false;
        handle_command(cmd);
    }
    ui_.redraw();
}

// Called when the user submits a new message: compact first if the estimated
// context has reached the configured percentage of the window, so the next
// request goes out comfortably below the limit.
void Agent::maybe_auto_compact() {
    if (!cfg_.auto_compact || cfg_.model.empty()) return;
    refresh_ctx_limit();
    if (ctx_limit_ <= 0) return;
    int used = estimate_ctx_used();
    size_t pct = static_cast<size_t>(used) * 100u / static_cast<size_t>(ctx_limit_);
    if (pct < cfg_.compaction_threshold) return;
    // Only worth compacting if messages exist beyond the retained tail;
    // otherwise compaction would replace nothing and could loop every turn.
    size_t from = session_.messages.size() > 4 ? session_.messages.size() - 4 : 1;
    while (from > 1 && session_.messages[from].role == Role::Tool) --from;
    size_t droppable = 0;
    for (size_t i = 1; i < from; ++i)
        if (session_.messages[i].role != Role::System) ++droppable;
    if (droppable == 0) return;
    ui_.message("[auto-compact] context ~" + util::fmt_tokens(static_cast<size_t>(used)) +
                "/" + util::fmt_tokens(static_cast<size_t>(ctx_limit_)) + " (" +
                std::to_string(pct) + "%) >= threshold " +
                std::to_string(cfg_.compaction_threshold) + "%");
    do_compact(true);
}

void Agent::run_turn(const std::string& text) {
    if (text.empty()) return;
    if (cfg_.model.empty()) {
        ui_.message("No model selected. Use /model", STYLE_YELLOW);
        if (!select_model_interactive()) return;
        if (cfg_.model.empty()) return;
    }
    cancel_all_ = false;

    // Compaction first, so the new message goes out below the threshold.
    maybe_auto_compact();

    Message user;
    user.role = Role::User;
    user.content = text;
    session_.messages.push_back(user);

    ui_.append("You:", STYLE_BOLD);
    ui_.append(text);
    ui_.append("");
    ui_.append("Agent:", STYLE_BOLD | STYLE_CYAN);
    ui_.redraw();
    save_session(false);  // keep the user message even if we crash mid-response

    run_generation();

    // A slash command typed while responding cancels generation and runs now.
    if (!pending_cmd_.empty()) {
        std::string cmd = std::move(pending_cmd_);
        pending_cmd_.clear();
        handle_command(cmd);
        ui_.redraw();
    }
}

void Agent::handle_prompt(const std::string& text) {
    if (text.empty()) return;
    run_turn(text);
    flush_queue();
}

// Send queued messages, one turn each, until the queue is empty or the
// agent loop was cancelled (Ctrl+C / slash command).
void Agent::flush_queue() {
    if (cancel_all_) queue_.clear();
    while (running_ && !cancel_all_ && !Terminal::got_sigterm.load() && !queue_.empty()) {
        std::string next = std::move(queue_.front());
        queue_.pop_front();
        ui_.set_queue(queue_.size());
        ui_.redraw();
        if (!next.empty() && next[0] == '/') handle_command(next);
        else run_turn(next);
    }
    if (cancel_all_) queue_.clear();
    ui_.set_queue(queue_.size());
    if (!queue_.empty()) ui_.redraw();
}

// Read keys while the model is generating. Called from the curl callbacks,
// so it must only touch the UI input line and the queue - never dispatch a
// prompt or a command.
void Agent::pump_input() {
    while (auto ev = input_.read(0)) {
        switch (ev->key) {
            case Key::Enter: {
                std::string line = util::trim(ui_.input_buf());
                ui_.push_history(line);
                ui_.set_input("");
                if (!line.empty() && !cancel_all_) {
                    if (line[0] == '/') {
                        // slash command: cancel the agent loop, run afterwards
                        pending_cmd_ = line;
                        abort_gen_ = true;
                        cancel_all_ = true;
                    } else {
                        queue_.push_back(line);
                        ui_.set_queue(queue_.size());
                    }
                }
                ui_.redraw();
                break;
            }
            case Key::CtrlC:
                abort_gen_ = true;
                cancel_all_ = true;
                ui_.set_input("");
                ui_.redraw();
                break;
            case Key::Char:
                if (ev->codepoint >= 32) {
                    ui_.input_insert(ev->codepoint);
                    ui_.redraw();
                }
                break;
            case Key::Backspace: ui_.input_backspace(); ui_.redraw(); break;
            case Key::Delete:    ui_.input_delete();    ui_.redraw(); break;
            case Key::Left:      ui_.input_left();      ui_.redraw(); break;
            case Key::Right:     ui_.input_right();     ui_.redraw(); break;
            case Key::Home:      ui_.input_home();      ui_.redraw(); break;
            case Key::End:       ui_.input_end();       ui_.redraw(); break;
            case Key::Up:        ui_.input_history_up();   ui_.redraw(); break;
            case Key::Down:      ui_.input_history_down(); ui_.redraw(); break;
            case Key::Escape:    ui_.set_input(""); ui_.redraw(); break;
            case Key::PageUp:    ui_.scroll_page_up();   ui_.redraw(); break;
            case Key::PageDown:  ui_.scroll_page_down(); ui_.redraw(); break;
            default: break;  // Tab and other control keys: ignored while busy
        }
    }
}

void Agent::run_generation() {
    generating_ = true;
    abort_gen_ = false;
    Terminal::got_sigint.store(false);
    Terminal::sigint_count.store(0);

    auto should_abort = [this]() -> bool {
        if (Terminal::got_sigterm.load()) return true;
        if (Terminal::got_sigint.load()) {
            abort_gen_ = true;
            cancel_all_ = true;
            Terminal::got_sigint.store(false);
        }
        pump_input();  // keep the input line alive while generating
        return abort_gen_.load();
    };

    nlohmann::json final_msg;
    std::string err;
    OllamaUsage usage;
    auto messages = build_messages();

    // Streaming display phase: 0 = nothing yet, 1 = thinking block (dim),
    // 2 = content block (normal). Thinking models open the dim block on the
    // first reasoning token; models that never think stream normally from
    // the first content token.
    int phase = 0;
    auto maybe_redraw = [this]() {
        static uint64_t last = 0;
        uint64_t now = util::steady_ms();
        if (now - last > 80) {
            ui_.redraw();
            last = now;
        }
    };
    ui_.redraw();  // "Agent:" header reaches the screen before the first token
    bool ok = ollama_->chat_stream(
        cfg_.model,
        messages,
        tool_definitions(),
        [this, &phase, &maybe_redraw](const std::string& tok) {
            if (phase == 1) {  // thinking block open: close it, then content
                ui_.end_stream();
                ui_.append("");
                ui_.begin_stream();
            } else if (phase == 0) {
                ui_.begin_stream();
            }
            phase = 2;
            ui_.stream_token(tok);
            maybe_redraw();
        },
        [this, &phase, &maybe_redraw](const std::string& tok) {
            if (phase == 2) return;  // content already started: drop late thinking
            if (phase == 0) ui_.begin_stream(STYLE_DIM);
            phase = 1;
            ui_.stream_token(tok);
            maybe_redraw();
        },
        nullptr,  // tool calls collected in final_msg
        should_abort,
        &err,
        &final_msg,
        &usage
    );

    ui_.end_stream();

    if (err == "interrupted") {
        // Cancelled (Ctrl+C or a slash command): drop the unfinished assistant
        // message, but persist everything that did complete (tool results etc).
        ui_.append("");
        ui_.message("(interrupted)", STYLE_YELLOW);
        generating_ = false;
        save_session(false);
        ui_.redraw();
        return;
    }
    if (!ok) {
        ui_.append("");
        ui_.message("Error: " + err, STYLE_RED);
        generating_ = false;
        save_session(false);  // keep the user message and prior turns
        ui_.redraw();
        return;
    }

    // Anchor Ollama's exact token counts to the history state that was just
    // sent (the session has not been extended with this response yet).
    if (usage.prompt_eval >= 0) {
        ctx_anchor_ = usage.prompt_eval + (usage.eval > 0 ? usage.eval : 0);
        ctx_anchor_chars_ = sent_chars();
        if (ctx_limit_ <= 0) refresh_ctx_limit();  // model is loaded now
        update_ctx_display();
    }

    // Store assistant message
    Message asst;
    asst.role = Role::Assistant;
    if (final_msg.contains("content") && final_msg["content"].is_string())
        asst.content = final_msg["content"].get<std::string>();
    if (final_msg.contains("thinking") && final_msg["thinking"].is_string())
        asst.thinking = final_msg["thinking"].get<std::string>();
    if (final_msg.contains("tool_calls"))
        asst.tool_calls = final_msg["tool_calls"];
    session_.messages.push_back(asst);
    save_session(false);  // every completed model response is persisted
    update_ctx_display();  // assistant tokens are now part of the history

    ui_.append("");  // blank after response
    ui_.redraw();

    // Process tool calls if any
    if (!cancel_all_ && !asst.tool_calls.is_null() && asst.tool_calls.is_array() &&
        !asst.tool_calls.empty()) {
        process_tool_calls(final_msg);
    }

    generating_ = false;
    save_session(false);  // silent auto-save; Ctrl-S / quit still confirm
}

void Agent::process_tool_calls(const nlohmann::json& assistant_msg) {
    if (!assistant_msg.contains("tool_calls")) return;
    for (const auto& tc : assistant_msg["tool_calls"]) {
        // Ctrl+C / slash cancel: stop the agent loop, no further tool calls
        if (cancel_all_ || !running_ || Terminal::got_sigterm.load()) return;
        std::string name;
        nlohmann::json args = nlohmann::json::object();
        std::string id = "call";
        if (tc.contains("id") && tc["id"].is_string()) id = tc["id"].get<std::string>();
        if (tc.contains("function")) {
            auto& fn = tc["function"];
            if (fn.contains("name")) name = fn["name"].get<std::string>();
            if (fn.contains("arguments")) {
                if (fn["arguments"].is_string()) {
                    try {
                        args = nlohmann::json::parse(fn["arguments"].get<std::string>());
                    } catch (...) {
                        args = nlohmann::json::object();
                    }
                } else {
                    args = fn["arguments"];
                }
            }
        }
        ui_.append("[tool] " + name, STYLE_YELLOW);
        if (args.is_object()) {
            for (auto it = args.begin(); it != args.end(); ++it) {
                std::string v = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
                if (v.size() > 120) v = v.substr(0, 120) + "...";
                ui_.append("  " + it.key() + ": " + v);
            }
        }
        ui_.redraw();

        auto result = tools_->run(name, args, [this](const std::string& prompt) {
            return ui_.confirm(input_, prompt);
        });

        ui_.append("[result]", 1);
        std::string out = result.output;
        if (out.size() > 2000) out = out.substr(0, 2000) + "\n[truncated]";
        ui_.append(out, out.rfind("ERROR:", 0) == 0 ? STYLE_RED : STYLE_NORMAL);
        ui_.append("");
        ui_.redraw();

        Message tool_msg;
        tool_msg.role = Role::Tool;
        tool_msg.content = result.output;
        tool_msg.tool_call_id = id;
        tool_msg.name = name;
        session_.messages.push_back(tool_msg);
    }
    save_session(false);  // tool results are persisted before the next round

    if (cancel_all_ || !running_ || Terminal::got_sigterm.load()) return;

    // Continue generation with tool results
    ui_.append("Agent:", STYLE_BOLD | STYLE_CYAN);
    run_generation();
}

int Agent::run() {
    init_session();
    update_status();
    if (cfg_.model.empty()) {
        ui_.append("Welcome to lumen. Select a model with /model or Ctrl-L.", 1);
        ui_.append("");
    }
    // Help only for a fresh/empty session. A restored conversation opens on
    // its replayed tail instead, so recent tool calls and results are the
    // first thing visible without scrolling.
    if (session_.messages.size() <= 1) show_help();
    ui_.redraw();

    while (running_) {
        if (Terminal::got_sigterm.load()) break;
        if (Terminal::got_sigwinch.exchange(false)) {
            ui_.on_resize();
        }

        // Double Ctrl-C to quit when not generating
        if (Terminal::sigint_count.load() >= 2 && !generating_) {
            break;
        }
        if (Terminal::got_sigint.exchange(false) && !generating_) {
            // first Ctrl-C at prompt: clear input
            ui_.set_input("");
            ui_.redraw();
        }

        auto ev = input_.read(100);
        if (!ev) continue;

        switch (ev->key) {
            case Key::CtrlQ:
                running_ = false;
                break;
            case Key::CtrlC:
                if (generating_) {
                    abort_gen_ = true;
                } else {
                    ui_.set_input("");
                    ui_.redraw();
                }
                break;
            case Key::CtrlD:
                if (ui_.input_buf().empty()) running_ = false;
                break;
            case Key::CtrlL:
                select_model_interactive();
                ui_.redraw();
                break;
            case Key::CtrlN:
                new_session();
                ui_.redraw();
                break;
            case Key::CtrlS:
                save_session();
                ui_.redraw();
                break;
            case Key::Enter: {
                std::string line = util::trim(ui_.input_buf());
                ui_.push_history(line);
                ui_.set_input("");
                if (line.empty()) {
                    ui_.redraw();
                    break;
                }
                if (line[0] == '/') {
                    handle_command(line);
                } else {
                    handle_prompt(line);
                }
                ui_.redraw();
                break;
            }
            case Key::Backspace:
                ui_.input_backspace();
                ui_.redraw();
                break;
            case Key::Tab:
                handle_tab();
                ui_.redraw();
                break;
            case Key::Left:
                ui_.input_left();
                ui_.redraw();
                break;
            case Key::Right:
                ui_.input_right();
                ui_.redraw();
                break;
            case Key::Home:
                ui_.input_home();
                ui_.redraw();
                break;
            case Key::End:
                ui_.input_end();
                ui_.redraw();
                break;
            case Key::Up:
                ui_.input_history_up();
                ui_.redraw();
                break;
            case Key::Down:
                ui_.input_history_down();
                ui_.redraw();
                break;
            case Key::PageUp:
                ui_.scroll_page_up();
                ui_.redraw();
                break;
            case Key::PageDown:
                ui_.scroll_page_down();
                ui_.redraw();
                break;
            case Key::Char:
                if (ev->codepoint >= 32) {
                    ui_.input_insert(ev->codepoint);
                    ui_.redraw();
                }
                break;
            case Key::Escape:
                ui_.set_input("");
                ui_.redraw();
                break;
            default:
                break;
        }
    }

    save_session();
    return 0;
}
