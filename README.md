# lumen

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

A lightweight terminal coding agent that runs on a **real Linux virtual console**
(`/dev/tty1`, `TERM=linux`) — no GPU, no browser, no modern terminal emulator
required. It drives a local [Ollama](https://ollama.com) model and can read,
write, edit and search files and run shell commands on your behalf.

lumen is intentionally small: a plain VT100/ANSI interface, a handful of
dependencies, and a memory footprint that leaves room for the model itself.

## Features

- **Runs on a real TTY** — raw termios + ANSI only, no ncurses, no xterm
  extensions. Content drawn before startup is restored on exit via the
  alternate screen buffer.
- **Tool calling** — `read_file`, `write_file`, `edit_file`, `list_directory`,
  `search` and `execute`, with confirmation for anything destructive.
- **Sessions** — persist, list, load, rename and delete conversations. Sessions
  are auto-named from the conversation and listed by last activity.
- **Streaming responses** with a live spinner and a queue for messages typed
  while the model is still working.
- **Context management** — automatic compaction near the context limit, with a
  structured hand-off summary.
- **Optional Markdown rendering** — headings, lists, quotes, tables, bold/
  italic/code and fenced code blocks with syntax highlighting.
- **`NO_COLOR` support** — disables all styling when set.

## Requirements

- Linux (POSIX)
- A C++17 compiler (`g++` or `clang++`)
- CMake ≥ 3.14
- libcurl
- [Ollama](https://ollama.com) reachable at `http://127.0.0.1:11434` by default

<details>
<summary>Debian / Ubuntu / Raspberry Pi OS</summary>

```bash
sudo apt update
sudo apt install -y build-essential cmake libcurl4-openssl-dev
```

</details>

## Build

```bash
git clone https://github.com/septicwolf818/lumen.git
cd lumen
make build
```

`make build` configures and compiles into `build/` with `CMAKE_BUILD_TYPE=Release`.
Pass `make build BUILD_TYPE=Debug` for a debug build.

### Install / uninstall

```bash
make install     # no sudo: installs to ~/.local (or /usr/local under sudo)
make uninstall   # removes the binary; optionally removes config and sessions
make clean       # removes the build directory
```

## Usage

```bash
ollama serve          # make sure the server is running
lumen                 # on a real TTY or any terminal
TERM=linux lumen      # explicitly exercise Linux-console behaviour
```

On first run, select a model with `/model` (or `Ctrl-L`).

## Key bindings

| Key          | Action                              |
|--------------|-------------------------------------|
| `Enter`      | Submit prompt                       |
| `Tab`        | Complete slash command / argument   |
| `Ctrl-C`     | Interrupt generation (twice: quit)  |
| `Ctrl-D`     | Exit (at empty prompt)              |
| `Ctrl-L`     | Model selection                     |
| `Ctrl-N`     | New session                         |
| `Ctrl-S`     | Save session                        |
| `Ctrl-Q`     | Quit                                |
| `Up` / `Down`| Command history / scroll            |
| `PageUp/Down`| Scroll conversation                 |
| `Home` / `End`| Top / bottom of conversation       |
| `Left`/`Right`| Move cursor in input               |
| `Esc`        | Clear input                         |

## Slash commands

```
/help                 Show help
/model [NAME]         List or select a model
/session              Show current session info
/session new [NAME]   Create a new session
/session list         List sessions (newest first, with last activity)
/session load NAME    Load a session
/session rename A B   Rename the current session
/session delete NAME  Delete a session
/cd PATH              Change working directory
/pwd                  Print working directory
/clear                Clear the conversation display
/compact              Summarise and compact context
/tools                List available tools
/config               Show configuration
/quit  /exit          Exit
```

`Tab` completes command names, `/session` subcommands, filesystem paths after
`/cd`, and session names after `/session load|delete|rename`. The first `Tab`
extends to the common prefix, further presses cycle through the matches.

When a session still has an auto-generated name and a model is configured,
lumen names it from the conversation after the first turn: the model's answer is
lower-cased, every run of non-alphanumeric characters becomes `-`, and a name
already in use gets a `-2`, `-3`, … suffix. Title generation never interrupts an
in-flight agent loop.

Paths passed to `/cd` and to tool `path` arguments may start with `~`.

## Configuration

`~/.config/lumen/config.json`:

```json
{
  "ollama_url": "http://127.0.0.1:11434",
  "model": "qwen2.5-coder",
  "auto_approve_tools": false,
  "allow_path_escape": false,
  "markdown": false,
  "max_file_bytes": 262144,
  "max_tool_output_bytes": 65536,
  "max_history_messages": 200,
  "auto_compact": true,
  "compaction_threshold": 80,
  "context_limit": 0,
  "last_session": "default"
}
```

| Key | Meaning |
|-----|---------|
| `ollama_url` | Ollama base URL. Overridable with `OLLAMA_HOST` / `OLLAMA_URL`. |
| `model` | Model to use; empty means "ask on start". |
| `auto_approve_tools` | Skip the `y/N` prompt for destructive tools. |
| `allow_path_escape` | Permit tools to access paths outside the working directory. |
| `markdown` | Render messages as Markdown (see below). |
| `max_file_bytes` | Read/write size cap for file tools. |
| `max_tool_output_bytes` | Truncation cap for tool output. |
| `max_history_messages` | Upper bound on messages kept per request. |
| `auto_compact` | Compact automatically near the context limit. |
| `compaction_threshold` | Percentage of the context window that triggers compaction. |
| `context_limit` | Override the detected context window (0 = detect from Ollama). |
| `last_session` | Session restored on startup. |

Environment override:

```bash
export OLLAMA_HOST=http://127.0.0.1:11434
```

## Markdown

Off by default so the harness stays lightweight. Enable with `"markdown": true`:

- `**bold**`, `*italic*`, `~~strikethrough~~` (the Linux console has no italic
  attribute, so emphasis is shown as colour)
- `` `inline code` ``, and fenced code blocks with a language header:
  ```
  python ----------------------------------------
  def add(a, b):
      return a + b
  ```
- syntax highlighting for C, C++, C#, Java, JavaScript/TypeScript (and
  JSX/TSX/Vue/Svelte), Go, Rust, Python, Ruby, PHP, Perl, Kotlin, Swift, Scala,
  Lua, R, Dart, Elixir, Erlang, Haskell, Clojure, Zig, shell, SQL, HTML, CSS,
  JSON and YAML - driven by keyword data in
  [`data/keywords.json`](data/keywords.json), regenerated with
  `tools/gen_keyword_data.py`
- ATX headings, unordered/ordered lists, blockquotes and thematic breaks
- GFM-style tables with `+`/`-`/`|` borders and per-column alignment
- links rendered as `text (url)`

Blocks are rendered as they stream: a fenced code block is highlighted from the
moment it opens, and the layout never jumps when the fence later closes.
Rendering uses only attributes the Linux console supports — no italics and no
clickable hyperlinks.

Output uses only bold, dim, underline, reverse and the 8 basic colours. Set
`NO_COLOR` (non-empty) to disable all styling:

```bash
NO_COLOR=1 lumen
```

Sessions are stored under `~/.local/share/lumen/sessions/`.

## Tools

| Tool             | Description                            | Confirmation |
|------------------|----------------------------------------|--------------|
| `read_file`      | Read a text file                       | No           |
| `list_directory` | List directory contents                | No           |
| `search`         | Recursive text search (ripgrep/grep)   | No           |
| `write_file`     | Create or overwrite a file             | Yes          |
| `edit_file`      | Replace a portion of a file            | Yes          |
| `execute`        | Run a shell command                    | Yes          |

Destructive tools require an explicit `y` unless `auto_approve_tools` is true.
Paths resolve relative to the working directory; traversal outside it is blocked
unless `allow_path_escape` is enabled.

## Architecture

- Raw termios + VT100/ANSI; alternate screen buffer (DEC 1049) for clean exit
- Streaming via Ollama `/api/chat` (`stream: true`)
- Application-owned scrollback buffer with Markdown rendering on top of a
  span-based line model
- Sessions and configuration as JSON via vendored
  [nlohmann/json](https://github.com/nlohmann/json)
- HTTP via libcurl

Source layout:

```
src/
  main.cpp        entry point and exit sequence
  agent.*         conversation loop, tools, compaction, titles
  ollama.*        Ollama HTTP client
  session.*       session model and persistence
  tools.*         file/shell tools
  ui.*            terminal UI (scrollback, status, input, Markdown mapping)
  markdown.*      self-contained Markdown parser and renderer
  keywords.*      loads the embedded language keyword data
  terminal.*      raw TTY control
  input.*         key/escape-sequence decoding
  completion.hpp  slash-command / argument completion
  config.*        configuration model
  util.*          paths, IO, UTF-8 helpers
data/
  keywords.json   language keyword data (embedded at build time)
tools/
  gen_keywords.cpp    generates the embedded keyword header
  gen_keyword_data.py regenerates data/keywords.json from upstream
```

## Testing on a real TTY

```bash
# Switch to a free virtual console (Ctrl+Alt+F2), log in, then:
TERM=linux lumen
```

Check scrolling, input and backspace, `Tab` completion, `Ctrl-C` interrupt,
model selection, streaming, tool calls with confirmation, session save/load,
terminal resize (`SIGWINCH`), and a clean exit (previous screen restored, then
`lumen: Bye! Session was <name>`).

## Contributing

Issues and pull requests are welcome. Please keep the project dependency-light
and avoid new third-party libraries unless there is a strong justification.

- Build with `make build`; the tree must compile warning-free under
  `-Wall -Wextra -Wpedantic`.
- Match the surrounding code style and comment density.
- Keep terminal output compatible with `TERM=linux`.

## License

[MIT](LICENSE) © 2026 Rafał W

The language keyword data in `data/keywords.json` is compiled from
[languages-keywords](https://github.com/radosslav/languages-keywords)
(MIT for source, CC-BY-4.0 for the compiled data), originally assembled by
[@leighmcculloch](https://github.com/leighmcculloch) and contributors.
