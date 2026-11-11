# Agent hooks

PowerShell hooks that an agent session runs around its own tool calls. They
hold the project rules that are easier to enforce than to remember, so every
machine that works on this repository behaves the same way.

| Hook | Event | What it does |
|---|---|---|
| `check_code_rules.ps1` | PostToolUse (Edit, Write) | Reads the file that was just written: no em-dash (U+2014) anywhere, no personal name or AI attribution in source, comments in English, `.bat`/`.cmd`/`.ps1` ASCII-only. Exit 2 hands the findings back so the next step fixes them. |
| `check_commit.ps1` | PreToolUse (Bash) | Blocks a `git commit` that carries a `Co-Authored-By` trailer or a personal name, that stages code without a test under `tests/`, or whose code files are newer than the last green gate run. `[no-test]` in the message skips the test gate. A commit that changes what judges the code (the hooks, `tools/gate_run.jdb`, `tools/verify.sh`, `tests/parity.sh`, a baseline, a CI workflow) or that removes or rewrites lines of a suite under `tests/gate` is blocked until its message carries `[gate-change]`; adding lines to a suite needs nothing. The files of `git commit -- a b` are read from the command. |
| `check_dangerous.ps1` | PreToolUse (Bash) | Blocks the shell shapes that destroy work by accident, among them `rm` with an unbraced variable. |
| `check_mcp.ps1` | SessionStart | Spawns the jdBasic MCP binary and sends an `initialize`, so a session learns at the start that `jdb_*` and the workshop are unavailable instead of finding out mid-task. |
| `mark_tests_ok.ps1` | PostToolUse (Bash) | Writes `.claude/.tests_ok` when a gate run came back green. `check_commit.ps1` reads its timestamp. `tools/gate_run.jdb` (and `tools/verify.sh`, which runs it) stamps the same marker itself after a run that is green on both backends. |
| `lab_reflex.ps1` | SessionStart | Prints the workshop manifest so the persistent lab is restored rather than rebuilt. |
| `check_co_reward.ps1` | PostToolUse | Logs co-processor use for the wins ledger. |
| `session_end.jdb` | SessionEnd | jdBasic. Collects what the session leaves open: commits since the session started, unpushed commits, uncommitted files, and the Kaiku tickets in `doing`/`review` of one jdTrakr project. Writes `.claude/last_session.md` and appends a line to `.claude/sessions.jsonl`. Never fails the session end. |

## Session wrap-up

A SessionEnd hook cannot talk to the agent any more, so the wrap-up travels
to the next session instead:

1. SessionStart (`startup` only) prints `.claude/last_session.md` from the
   previous session and writes the start time to `.claude/session_started`.
2. SessionEnd runs `session_end.jdb <trakr-project>`, which counts commits
   since that start time (since midnight when the file is missing).

```json
"SessionEnd": [{ "hooks": [{ "type": "command",
  "command": "\"$CLAUDE_PROJECT_DIR/build/jdBasic.exe\" \"$CLAUDE_PROJECT_DIR/tools/hooks/session_end.jdb\" jdbasic >/dev/null 2>&1 || true",
  "timeout": 30 }] }]
```

The script takes the trakr project and the repository as arguments and has
nothing jdBasic-specific in it. It is meant to move to user level
(`~/.claude/settings.json`) for every project, with the trakr key read from
the project's CLAUDE.md or a small per-project file. Two sessions in the same
repository share `session_started` and `last_session.md`; the later one wins.
`sessions.jsonl` is the raw feed for the planned daily/weekly status reports.

## Wiring them up

The hooks are referenced from `.claude/settings.json`, which stays out of the
repository. One entry per hook, all of the same shape:

```json
{
  "matcher": "Edit|Write",
  "hooks": [
    {
      "type": "command",
      "command": "powershell -NoProfile -ExecutionPolicy Bypass -File \"$CLAUDE_PROJECT_DIR/tools/hooks/check_code_rules.ps1\"",
      "timeout": 10
    }
  ]
}
```

`$CLAUDE_PROJECT_DIR` is the repository root, so the same settings work on
every machine.

## Personal names

`check_code_rules.ps1` and `check_commit.ps1` reject personal names, which
means they need to know them. The names live in
`.claude/private_names.txt`, one per line, `#` for a comment:

```
# One name per line.
Somebody
```

The checkout ignores that file, so the names never reach the repository. With
no file present both hooks skip the name check and every other rule still
applies.

## Encoding

`check_code_rules.ps1` is UTF-8 with a BOM and ASCII-only on purpose: its
patterns build non-ASCII characters from codepoints (`[char]0x2014`) rather
than spelling them out, because Windows PowerShell 5.1 decodes a literal
against the console codepage and would mis-read them. Keep it that way when
editing.
