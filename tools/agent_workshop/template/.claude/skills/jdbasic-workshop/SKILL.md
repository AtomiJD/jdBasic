---
name: jdbasic-workshop
description: Work in this project's persistent jdBasic workshop over the jdbasic MCP server - a VM that keeps data and self-built helper functions across calls and across sessions, that can pause a running program so it can be inspected, patched and resumed, and whose code compiles to a native executable. Use for data exploration, calculations, file and text processing, charts, prototypes and small tools, and whenever writing jdBasic code. Read rules.md in this folder before the first line of jdBasic.
---

# The jdBasic workshop

The `jdbasic` MCP server is not a sandbox that forgets. It is a workshop:

1. **Warm state** - data loaded once stays in the VM for the whole session.
2. **Self-extension** - a missing capability is a FUNC you have not written yet.
   Write it in the VM, test it on the live data, keep it.
3. **Memory** - `jdb_savews` writes data and functions to `<name>.jsws` in the
   project folder; `jdb_loadws` brings both back in the next session.
4. **Live programs** - a running program can be paused, inspected, patched
   and resumed without a restart.
5. **Eyes** - render charts to PNG and look at them before showing them.

Read `rules.md` (next to this file) before writing jdBasic. jdBasic is not
like other BASICs; the rules list the mistakes every model makes.

## Session start

The SessionStart hook prints a `[jdBasic workshop]` block with this
project's workspace name and what the saved workspace holds.

1. Saved workspace listed: `jdb_loadws name="<name>"`, then `jdb_funcs`.
   Read `WORKSHOP_NOTES$` if the hook printed notes. Report in two lines
   what came back.
2. "No saved workspace yet": a fresh workshop. Start clean.
3. No `[jdBasic workshop]` block, or a line saying jdBasic did not start:
   the workshop is unavailable. Say so before relying on any `jdb_*` tool.

`jdb_loadws` resets the VM. Never call it after unsaved work.

## Working in the VM

- Load data once (`CSVREADER`, `TXTREADER$`, `JSON.PARSE$`, `IMPORT DF`),
  then operate on it with `jdb_eval`. Never reload per question.
- **Helpers are FUNCs that take data as parameters.** A FUNC defined in a
  later `jdb_eval` cannot read a global created in an earlier one (it reads
  NONE). Pass the data in.
- Give your helpers a short prefix (`WS_PROFILE`, `WS_PLOT`) so they never
  collide with builtins.
- Before defining: `jdb_check` the source, then `jdb_eval` it, then a
  one-line smoke test on the real data.
- Every global ends up in the saved workspace. Build throwaway test data
  inside the call (`PRINT WS_PROFILE([[1, "a"]], ["n", "s"])`), not as a
  named global.
- `jdb_doc NAME` before the first use of any builtin. Never guess an
  argument order or whether something is 0- or 1-based.

## Keep the data out of the chat

- Use `jdb_eval`'s `result` parameter for values you need: the expression
  comes back as pure JSON. No PRINT parsing.
- Never print a whole dataset. Check the size first (`LEN`, `LENV`), then
  print `TAKE(10, x)`, aggregates or a chart.
- `jdb_vars` shows every variable with its shape and a short preview.
- Long chunks: `jdb_eval` has a 30 s watchdog (`timeout_ms`, 0 = off). A
  chunk that times out is parked, the VM stays usable.

## Persistence

- `jdb_savews name="<name>"` at every milestone (a helper proven, a step of
  the analysis done) and always before the session ends.
- Keep a `WORKSHOP_NOTES$` variable up to date: one or two lines on what
  the workspace holds and what is still open. The next session's hook
  prints it.
- Saved: globals and the source of every successful `jdb_eval`. Not saved:
  empty values, and functions that came from `jdb_load` or `jdb_recompile`
  (load the file again after `jdb_loadws`).

## Live programs: stop, patch, resume

For a program in a `.jdb` file, not a chunk of `jdb_eval`:

1. `jdb_load path` starts it. It runs until it reaches a `STOP` statement
   or you call `jdb_stop`; `jdb_status` says `running`, `stopped` or `idle`.
2. While stopped, `jdb_eval` reads and changes its state. The locals of the
   stopped frame appear as globals.
3. To change logic: edit the file, then `jdb_recompile`. It replaces the
   FUNC and SUB bodies; top-level statements are not re-applied, so keep the
   logic that should change inside FUNCs and SUBs.
4. `jdb_resume` continues from where it stopped, with the new code and the
   changed values.

What a loaded program PRINTs does not come back to you. Keep the state you
want to watch in globals and read it with `jdb_eval` and `result`.

## Charts: render, look, fix

```basic
IMPORT SVG
DIM c = SVG.CHART("line")
SVG.SETOPT(c, "title", "Revenue per day")
SVG.LABELS(c, day_labels)
SVG.SERIES(c, "revenue", revenue)
SVG.WRITEPNG(SVG.RENDER(c), "C:/abs/path/revenue.png", {"scale": 2})
```

Kinds include `line`, `bar`, `scatter`, `pie`. Use an absolute path, then
open the PNG and look at it: overlapping labels, wrong scale, clipped
bars. Fix and render again before showing it. No window or graphics build
is needed.

## From workshop to program

What works in the workshop becomes a file and then an executable:
`jdbasic -c file.jdb` compiles it (the native build; on Windows it needs
the MSVC linker). The compiler is strict: every variable needs `DIM`,
which loose workshop code often lacks - `jdb_check` and fix before
compiling. Run shell commands with your own shell tool or `jdb_run_native`.

## Safety

- Never call `OS.EXEC` inside `jdb_eval`; it can deadlock the stdio server.
  Use your own shell tool.
- An eval that hangs: `jdb_stop`, `jdb_status`, then retry smaller. Do not
  restart the server mid-session; the state lives in it.
- `jdb_eval` and `jdb_run_native` run code with your user's rights on this
  machine, like a shell.
