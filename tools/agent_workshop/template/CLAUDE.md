@AGENTS.md

In Claude Code the `jdbasic-workshop` skill carries these instructions, and
the SessionStart hook prints the workspace name and its contents as a
`[jdBasic workshop]` block. If that block is missing or says jdBasic did not
start, tell the user the workshop is unavailable before using any `jdb_*`
tool.
