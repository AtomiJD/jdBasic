# Agent hooks

PowerShell hooks that an agent session runs around its own tool calls. They
hold the project rules that are easier to enforce than to remember, so every
machine that works on this repository behaves the same way.

| Hook | Event | What it does |
|---|---|---|
| `check_code_rules.ps1` | PostToolUse (Edit, Write) | Reads the file that was just written: no em-dash (U+2014) anywhere, no personal name or AI attribution in source, comments in English, `.bat`/`.cmd`/`.ps1` ASCII-only. Exit 2 hands the findings back so the next step fixes them. |
| `check_commit.ps1` | PreToolUse (Bash) | Blocks a `git commit` that carries a `Co-Authored-By` trailer or a personal name, that stages code without a test under `tests/`, or whose code files are newer than the last green gate run. `[no-test]` in the message skips the test gate. |
| `check_dangerous.ps1` | PreToolUse (Bash) | Blocks the shell shapes that destroy work by accident, among them `rm` with an unbraced variable. |
| `check_mcp.ps1` | SessionStart | Spawns the jdBasic MCP binary and sends an `initialize`, so a session learns at the start that `jdb_*` and the workshop are unavailable instead of finding out mid-task. |
| `mark_tests_ok.ps1` | PostToolUse (Bash) | Writes `.claude/.tests_ok` when a gate run came back green. `check_commit.ps1` reads its timestamp. |
| `lab_reflex.ps1` | SessionStart | Prints the workshop manifest so the persistent lab is restored rather than rebuilt. |
| `check_co_reward.ps1` | PostToolUse | Logs co-processor use for the wins ledger. |

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
