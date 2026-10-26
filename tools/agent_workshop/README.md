# jdBasic Agent Workshop

Give your coding agent a workshop that does not forget.

Most code-execution tools start a fresh interpreter for every call. The
jdBasic MCP server keeps one VM alive: the agent loads your data once,
writes its own helper functions, saves both to a workspace file and picks
them up again in the next session. It can pause a running program, look
at its variables, patch a function and let it continue. What works in the
workshop compiles to a native executable.

This folder turns any project into such a workshop for Claude Code, and
works with every other MCP client too.

## Quickstart (Claude Code, 3 minutes)

1. **Make `jdbasic` findable.** Either add the folder that holds
   `jdBasic.exe` (or `jdbasic`) to your `PATH`, or set the environment
   variable `JDBASIC` to its full path:

   ```
   setx JDBASIC "C:\tools\jdbasic\jdBasic.exe"      (Windows, new terminal after)
   export JDBASIC=/opt/jdbasic/jdbasic               (Linux / macOS)
   ```

   `setx` only reaches terminals opened afterwards: open a new terminal
   before the next step. Check there: `jdbasic --version` (or
   `"%JDBASIC%" --version`) lists `MCP`. `JDBASIC` wins over `PATH`, so a
   wrong `JDBASIC` breaks the workshop even when `jdbasic` is on `PATH`.

2. **Copy `template/` into your project root.** It contains `.mcp.json`,
   `AGENTS.md`, `CLAUDE.md` and a `.claude/` folder. If your project already
   has one of these files, merge by hand: the `jdbasic` entry in
   `.mcp.json`, the `SessionStart` hook and the permissions in
   `.claude/settings.json`, and the `@AGENTS.md` line in `CLAUDE.md`.

3. **Start Claude Code in the project** and approve the project MCP server
   `jdbasic` when asked. The first thing in the session is a
   `[jdBasic workshop]` block naming the workspace.

4. **Try it:**

   > Load sales.csv into the workshop, write a helper that profiles each
   > column, and save the workspace.

   Close the session, open a new one:

   > What's in the workshop?

   The hook has already listed the saved functions and variables; the agent
   restores them with one `jdb_loadws` call and continues where it stopped.

## What is in the template

| File | Purpose |
|---|---|
| `.mcp.json` | Registers the `jdbasic` stdio server (`jdbasic --mcp`). |
| `AGENTS.md` | What the workshop is and how the workspace is named; read by most agents. |
| `CLAUDE.md` | Imports `AGENTS.md` for Claude Code. |
| `.claude/skills/jdbasic-workshop/SKILL.md` | How to work in the workshop: warm state, helpers, persistence, stop/patch/resume, charts. |
| `.claude/skills/jdbasic-workshop/rules.md` | The jdBasic rules models get wrong: reserved names, indexing, syntax. |
| `.claude/hooks/workshop_start.sh` | SessionStart hook: finds jdbasic like `.mcp.json` does and says why when it cannot. Needs bash (on Windows: Git Bash from Git for Windows). |
| `.claude/hooks/workshop_start.jdb` | Run by the hook, written in jdBasic: names the workspace and summarises what it holds. |
| `.claude/settings.json` | Wires the hook and allows the read-only tools without prompts. |

The workspace file is `<project-folder-name>.jsws` in the project root. It
holds your data; add `*.jsws` to `.gitignore` unless you mean to share it.

## Other MCP clients

Cursor, Windsurf, Cline, Continue, Zed and the rest take the same server
entry (clients with a TOML config, such as Codex, the same two fields):

```json
{ "mcpServers": { "jdbasic": { "command": "/full/path/to/jdbasic", "args": ["--mcp"] } } }
```

Use the full path there; the `${JDBASIC:-jdbasic}` form in `.mcp.json` is
Claude Code syntax. Skills and hooks are Claude Code features; other
agents get the same instructions from `AGENTS.md`, which points to the two
skill files. Without the hook, the agent checks for `<name>.jsws` itself.

## Stop, patch, resume

The part no other language server offers. Save this as `demo.jdb` in the
project:

```basic
DIM rate = 2
DIM trail = []
FUNC value_at(i, r)
    RETURN i * r
END FUNC
FOR i = 1 TO 5
    IF i = 3 THEN STOP
    trail = APPEND(trail, value_at(i, rate))
NEXT i
```

Then ask the agent:

> Load demo.jdb. When it stops, set rate to 5, change value_at to
> multiply by 100, recompile, resume, and show me trail.

The agent calls `jdb_load`, `jdb_eval "rate = 5"`, edits the file,
`jdb_recompile`, `jdb_resume`, and reads `trail`: `[2, 4, 1500, 2000, 2500]`.
The first two steps ran with the old code, the rest with the new one, in
the same run.

## Native executables

`jdbasic -c file.jdb` compiles a program to an executable. It needs the
native build of jdBasic (the `mcp-native` pack) and, on Windows, the MSVC
linker from Visual Studio 2022 17.10 or newer. The compiler is strict:
every variable needs a `DIM`. The MCP server itself needs neither.

## Security

`jdb_eval` and `jdb_run_native` run code with your rights on your machine,
like a shell. Use the server locally, under your own user.

More: `doc/MCP.md` (all tools and client configs), `doc/languages.md` (the
language reference that `jdb_doc` searches).
