# lumen

A small, lightweight terminal chat/coding agent for real Linux virtual consoles (TTY)
such as `/dev/tty1`. Designed for low-resource hardware (Raspberry Pi 3 A+ / 512 MB).

It talks to a local [Ollama](https://ollama.com) server and supports tool calling
(read/write/edit files, list dirs, execute commands, search).

## Design goals

- Works with `TERM=linux` on a real virtual console
- No dependency on modern terminal emulators (Kitty, Alacritty, etc.)
- Minimal dependencies, low memory, fast startup
- Plain text UI – no fancy TUI frameworks
- Safe tool execution with confirmation for destructive actions
- Session persistence

## Requirements

- Linux (POSIX)
- C++17 compiler (g++ / clang++)
- CMake >= 3.14
- libcurl
- Ollama running locally (default `http://127.0.0.1:11434`)

### Raspberry Pi OS

```bash
sudo apt update
sudo apt install -y build-essential cmake libcurl4-openssl-dev
```

## Build

```bash
git clone https://github.com/septicwolf818/lumen.git
cd lumen
make build
```

`make build` configures and builds into `build/` with `CMAKE_BUILD_TYPE=Release`
by default:

```bash
make build BUILD_TYPE=Debug
```

Other targets:

```bash
make install    # no sudo needed: installs to ~/.local (or /usr/local under sudo)
make uninstall  # remove the binary; asks about config and sessions too
make clean      # remove the build directory
```

The binary is small and self-contained (plus libcurl).

## Run

```bash
# Ensure Ollama is running
ollama serve   # or system service

# On a real TTY or any terminal
lumen

# Explicitly test Linux console behaviour
TERM=linux lumen
```

## Key bindings

| Key          | Action                          |
|--------------|---------------------------------|
| Enter        | Submit prompt                   |
| Tab          | Complete slash command          |
| Ctrl+C       | Interrupt generation / first press |
| Ctrl+C again | Exit                            |
| Ctrl+D       | Exit (at empty prompt)          |
| Ctrl+L       | Model selection                 |
| Ctrl+N       | New session                     |
| Ctrl+S       | Save session                    |
| Ctrl+Q       | Quit                            |
| Up / Down    | Command history / scroll        |
| PageUp/Down  | Scroll conversation             |
| Home / End   | Top / bottom of conversation    |
| Left / Right | Move cursor in input            |
| Backspace    | Delete character                |
| Esc          | Cancel / clear multiline        |

## Slash commands

```
/help                 Show help
/model [NAME]         List or select model
/session              Session info
/session new [NAME]   Create new session
/session list         List sessions
/session load NAME    Load session
/session rename A B   Rename
/session delete NAME  Delete session
/cd PATH              Change working directory
/pwd                  Print working directory
/clear                Clear conversation display
/compact              Summarise + compact context
/tools                List available tools
/config               Show configuration
/quit  /exit          Exit
```

Press `Tab` while typing a command to complete it (`/mo<Tab>` -> `/model`).
The first `Tab` shows all matches and extends to their common prefix, further
presses cycle through them. `/session` subcommands complete the same way
(`/session l<Tab>` -> `/session list`).

When a session still has an auto-generated name and a model is configured,
lumen names it from the conversation after the first turn: the model's answer is
lower-cased, every run of non-alphanumeric characters becomes `-`, and a name
already in use gets a `-2`, `-3`, ... suffix.

Paths passed to `/cd` and to tool `path` arguments may start with `~`, which
expands to your home directory (`/cd ~/projects`).

## Configuration

`~/.config/lumen/config.json`

```json
{
  "ollama_url": "http://127.0.0.1:11434",
  "model": "qwen2.5-coder",
  "auto_approve_tools": false,
  "max_file_bytes": 262144,
  "max_tool_output_bytes": 65536,
  "allow_path_escape": false
}
```

Environment override:

```bash
export OLLAMA_HOST=http://127.0.0.1:11434
```

Output uses only the ANSI attributes the Linux console supports (bold, dim and
the 8 basic colours). Set `NO_COLOR` (non-empty) to disable all colour and
styling:

```bash
NO_COLOR=1 lumen
```

Sessions are stored under:

```
~/.local/share/lumen/sessions/
```

## Tools

The model can request:

| Tool            | Description                     | Confirmation |
|-----------------|---------------------------------|--------------|
| `read_file`     | Read a text file                | No           |
| `list_directory`| List directory                  | No           |
| `search`        | Recursive text search (ripgrep/grep) | No      |
| `write_file`    | Create/overwrite a file         | Yes          |
| `edit_file`     | Replace a portion of a file     | Yes          |
| `execute`       | Run a shell command             | Yes          |

Destructive tools require explicit `y` confirmation unless `auto_approve_tools` is true.

Paths are resolved relative to the current working directory. Path traversal outside
the working directory is blocked unless `allow_path_escape` is enabled.

## Architecture notes

- Raw termios + ANSI/VT100 only (no ncurses)
- Streaming responses via Ollama `/api/chat` with `stream: true`
- Simple scrollback buffer owned by the application
- JSON sessions via nlohmann/json (header-only, vendored)
- libcurl for HTTP

## Testing on real TTY

```bash
# Switch to a free virtual console (Ctrl+Alt+F2 etc.)
# Login, then:
TERM=linux lumen
```

Verify:

- Scrolling (PageUp/PageDown)
- Input + Backspace + arrows
- Tab completion of slash commands
- Ctrl-C interrupt
- Model list / selection
- Streaming tokens
- Tool calls + confirmation
- Session save/load
- Terminal resize (SIGWINCH)
- Clean exit (screen restored, `lumen: Bye! Session was <name>` printed)

## License

MIT
