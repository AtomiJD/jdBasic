---
name: jdlab
description: Workshop mode for data sessions on the jdBasic MCP - a persistent VM with memory and eyes. Activate when Atomi hands over a dataset or says "lab" / "workshop". Locks Claude into the workshop loop - restore the mylab workspace first, keep all data in the VM, BUILD missing capabilities as LAB_ FUNCs instead of working around them in chat, visually verify every chart via screenshot before showing it, and savews at every milestone. `/jdlab record` adds jdvibe-style terseness for recording sessions.
---

# /jdlab - the Workshop

The jdBasic MCP VM is not a sandbox that forgets. It is a **workshop**: state is
warm across every `jdb_eval`, tools you build for yourself persist via
`jdb_savews` / `jdb_loadws` together with the data, and `GFX.SAVE_SCREENSHOT`
+ Read gives you eyes on your own output. Work accordingly:

1. **Warm state** - load data ONCE, then operate on it in the VM all session.
2. **Self-extension** - a missing capability is a FUNC you haven't written yet.
3. **Eyes** - never show a chart you haven't looked at yourself.

`jdbwrite` rules apply to every line of jdBasic in this mode. Read that skill
first if you haven't this session.

## Session start protocol (always, before anything else)

1. `jdb_loadws name="mylab"` - try to restore the workshop. A missing
   workspace returns an error and leaves the VM untouched (servers older
   than 2026-06-11 reported success on a no-op - verify with step 2 anyway).
2. `jdb_funcs` - if the LAB_ helpers are listed, the restore is real. Then
   `jdb_eval "PRINT LAB_MANIFEST$"` for the workspace summary (see below)
   and report in 2-4 lines what came back. Continue from there.
3. On a load error: say "fresh workshop" and start clean.

`jdb_vars` previews each value (arrays as `ARRAY [shape] …`, capped at
`max_chars`, default 160) - safe even with a big dataset loaded. Still
maintain a `LAB_MANIFEST$` string variable as the curated summary: every
milestone, set it to a short inventory ("sales: 20000x5 [day region product
units revenue]; helpers: LAB_PROFILE LAB_HIST LAB_PLOT; last: monthly
revenue chart"). It persists with savews, so a cold session reads one
variable and knows the full story - including the open question.

`jdb_loadws` RESETS the VM (when the file exists) - never call it
mid-session after unsaved work.

## Build-don't-ask reflex

When a capability is missing (profiling, binning, a plot style, an export),
do NOT work around it in chat, do NOT compute it piecemeal with throwaway
evals, do NOT ask permission. **Write a jdBasic FUNC for it in the VM**
(`jdb_eval` with the full FUNC body), test it on the live data, keep it.
Helpers accumulate over sessions - that is the point of the workshop.

Quality rules for self-built helpers:

- **Naming**: English identifiers only; every helper is prefixed `LAB_`
  (`LAB_PROFILE`, `LAB_HIST`, `LAB_PLOT`). String-returning helpers end in `$`
  (`LAB_FMT$`). No reserved names ever - probe-eval an unfamiliar name first
  (known traps: `PI E TAU TRUE FALSE NULL INF NAN CLS STEP LINE ON TICK VAL
  LEN COUNT SORT SUM MIN MAX`).
- **Signatures verified**: `jdb_doc` every builtin before first use in a
  helper. Never guess arg order (DATEADD!), 0/1-base (IOTA is 1-based,
  MID$/INSTR are 0-based), or return shape.
- **APL before loops**: `AGG`, `TALLY`, `SELECT`, `FILTER`, `GRADE`, `IOTA`,
  element-wise ops. A `FOR` loop is the fallback.
- **Lint before define**: `jdb_check` the FUNC source, then `jdb_eval` it,
  then a one-line smoke test on real data.
- Comments: functionality only, sparse.

## See-then-fix loop (every render)

Never present a chart, grid, or layout you haven't seen. The loop is:

1. Render in the VM; call `GFX.SAVE_SCREENSHOT("D:/usr/dev/cc/tmp/<name>.png")`
   **BEFORE `SCREENFLIP`** (after the flip the back buffer is cleared - a
   black PNG means you captured too late). Absolute paths always.
2. `Read` the PNG - actually look: labels overlapping? axis scale wrong? bars
   clipped? text off-screen? colors unreadable?
3. If anything is off, fix the helper and re-render - **one self-correction
   pass minimum** before showing the result to Atomi. Say what you saw and
   what you changed in one line.
4. Clean up throwaway captures from `tmp/` at session end; keep only the
   final shots.

**Charts come from the CHART module** (`modules/chart.jdb`): `IMPORT CHART`
once, then `CHART.PLOT(xs, ys, title$, path$)`, `CHART.SCATTER`,
`CHART.BARS(labels, values, ...)`, `CHART.HIST(vals, n_bins, ...)` - margins,
gridlines and tick labels are built in. Self-built LAB_ render helpers are
for needs the module doesn't cover; keep them parameterised by output path
so re-rendering after a fix is a single eval.

## Persistence discipline

- `jdb_savews name="mylab"` at **every milestone** (new helper proven, analysis
  step done) and ALWAYS at session end. Saving is cheap; losing the workshop
  is not.
- The `.jsws` lands in the repo root (`D:\usr\dev\cc\mylab.jsws`); it is
  gitignored, as is `tmp/`.
- savews persists globals + FUNC/SUB definitions. It skips builtin constants
  and **empty values** - don't rely on an empty-string flag variable
  surviving a reload.
- After any `jdb_loadws`, re-verify with `jdb_funcs` + `PRINT LAB_MANIFEST$`
  before assuming a helper exists.
- **Helpers take data as parameters.** A FUNC defined in a later eval than a
  global cannot read it (NONE) - scope binding is per eval chunk. Same-chunk
  FUNC+DIM works, and a savews/loadws cycle re-bases everything into one
  scope, but parameter-passing is the rule that never surprises.

## Token discipline - the data stays in the VM

The dataset lives in the VM, not in the chat context. Only summaries,
screenshots, and small extracts cross the wire:

- **Structured values: use `jdb_eval`'s `result` parameter** (servers from
  2026-06-12 on) - the expression is evaluated after the chunk and returned
  as a pure-JSON block. No PRINT-parsing, no formatting loss. Cap is 100k
  chars; narrow with TAKE/SLICE/aggregates.
- Long-running chunks: `jdb_eval` has a 30s watchdog (`timeout_ms`,
  0 = off). A timed-out chunk is parked, the VM stays usable.

- NEVER `PRINT` a whole dataset or a full column. Check size first
  (`LEN`, `LENV` for the full shape) - print head slices (`TAKE(10, ...)`), aggregates,
  `TALLY`/`AGG` tables, `FRMV$` of small matrices.
- Build summarising helpers (`LAB_PROFILE` returns per-column stats, not
  rows) so the natural output is already small.
- Charts replace row dumps: render + screenshot instead of printing numbers.
- Mind allocation sizes before vectorised ops - no billion-element IOTA.

## Hard safety rules (from the MCP server's nature)

- **NEVER call `OS.EXEC` / `OS.LOAD` inside `jdb_eval`** - it deadlocks the
  stdio server. Shell work happens in Claude's own Bash tool.
- Never restart the VM mid-session. If an eval hangs: `jdb_stop` →
  `jdb_status` → retry smaller.
- `CSVREADER` reads **numbers only**. String columns: `TXTREADER$` + `SPLIT`
  per line, or keep categorical columns numerically coded with a small
  lookup array.
- Screenshots before `SCREENFLIP`/`GL.FLIP`, absolute paths into `tmp/`.

## `/jdlab record` - recording sub-mode

When Atomi invokes `/jdlab record` (or jdlab during a recording), layer
jdvibe-style terseness on top of everything above:

- **Lead with the tool call.** The viewer reads the `jdb_eval` /
  `jdb_savews` / Read-PNG sequence as the story. No preamble.
- One-line confirmations only: `restored - 3 helpers + sales (20k rows)`,
  `built - LAB_HIST`, `saw overlapping x-labels - rotated, re-rendered`,
  `saved - mylab`.
- No build advice, no doc-check narration, no "should I...?". The setup is
  verified before record; do what is asked.
- All terminal output in English (international audience); Atomi may speak
  German in chat.
- The see-then-fix loop is the money shot - when you spot a flaw in your own
  render, SAY the one-liner (what you saw → what you change), fix, re-render.
  Don't skip the verbalisation; the viewer can't read your mind.

Exit the sub-mode when Atomi says "cut" / "stop recording" / clearly ends the
take. A hard crash trumps terseness - debug normally, then return.

## Tool map for this mode

| Tool | When |
|------|------|
| `jdb_loadws`  | first action of the session (name="mylab") |
| `jdb_eval`    | everything: load data, define LAB_ FUNCs, analyse, render |
| `jdb_check`   | lint every FUNC body before defining it |
| `jdb_doc`     | verify any builtin before first use |
| `jdb_funcs` | after loadws (the real restore check), and to recap helpers |
| `jdb_vars`  | quick inventory (shape + preview, `max_chars` opt.); `LAB_MANIFEST$` stays the curated summary |
| `jdb_savews`  | every milestone + session end (name="mylab") |
| `jdb_stop` / `jdb_status` | only when an eval hangs |
| Read (PNG)    | the seeing step after every GFX.SAVE_SCREENSHOT |

`jdb_load` / `jdb_recompile` / `jdb_run_native` are for script projects, not
the workshop - stay on `jdb_eval`.
