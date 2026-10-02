#pragma once

#include "config.hpp"
#include "session.hpp"
#include "ollama.hpp"
#include "tools.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "terminal.hpp"
#include "completion.hpp"
#include <atomic>
#include <string>
#include <vector>
#include <deque>
#include <memory>

class Agent {
public:
    Agent(Terminal& term, UI& ui, InputReader& input);

    int run();

private:
    Terminal& term_;
    UI& ui_;
    InputReader& input_;
    Config cfg_;
    Session session_;
    std::unique_ptr<OllamaClient> ollama_;
    std::unique_ptr<ToolRunner> tools_;
    std::atomic<bool> generating_{false};
    std::atomic<bool> abort_gen_{false};
    bool running_ = true;

    // Tab completion cycling state: active while the user keeps pressing Tab
    // on the same token; any edit invalidates it.
    struct TabCycle {
        bool active = false;
        size_t start = 0;
        std::string token;                  // token written by the first Tab
        std::vector<std::string> matches;
        size_t idx = 0;
        bool started = false;               // a candidate has been selected
    };
    TabCycle tab_cycle_;

    // Messages typed while the model is responding: sent after it finishes.
    std::deque<std::string> queue_;
    // Slash command typed while responding: cancels generation, runs after.
    std::string pending_cmd_;
    // Cancel requested (Ctrl+C or slash): stop generation, tool rounds and queue.
    bool cancel_all_ = false;

    // Context tracking for the status bar and auto-compaction.
    // ctx_anchor_ tokens were confirmed by Ollama (prompt_eval_count +
    // eval_count) for the history state of ctx_anchor_chars_ bytes; anything
    // added since is estimated at ~4 chars/token. limit 0 = unknown.
    std::string ctx_model_;      // model the tracking state belongs to
    int ctx_limit_ = 0;          // allocated window (config or /api/ps)
    int ctx_anchor_ = -1;        // exact tokens at the last reported usage
    size_t ctx_anchor_chars_ = 0;

    void init_session();
    void update_status();
    void handle_command(const std::string& line);
    void handle_tab();
    void handle_prompt(const std::string& text);
    void run_turn(const std::string& text);       // one user turn + generation
    void pump_input();                            // read keys while generating
    void flush_queue();                           // send queued messages
    void replay_message(const Message& m);        // redraw one stored message
    void run_generation();
    void process_tool_calls(const nlohmann::json& assistant_msg);
    bool select_model_interactive();
    void show_help();
    void do_compact(bool automatic = false);
    void maybe_auto_compact();                    // threshold check, new turn
    size_t sent_chars() const;                    // bytes of what we would send
    int estimate_ctx_used() const;                // anchor + estimated additions
    void refresh_ctx_limit();                     // detect model switch / /api/ps
    void update_ctx_display();                    // push "12.4k/32k (39%)" to UI
    void reset_ctx_tracking();                    // drop anchor + limit
    bool save_session(bool notify = true);
    void new_session(const std::string& name = "");
    void load_session(const std::string& name);
    std::vector<nlohmann::json> build_messages() const;
    std::string system_prompt() const;
};
