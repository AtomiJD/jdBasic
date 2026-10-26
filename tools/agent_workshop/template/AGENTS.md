# jdBasic workshop

This project has a persistent jdBasic workshop: the `jdbasic` MCP server
(`jdbasic --mcp`). Its VM keeps data and self-written functions between
calls, saves both to a workspace file for the next session, can pause a
running program for inspection and patching, and compiles finished code to
a native executable.

Use it for data exploration, calculations, file and text processing,
charts, prototypes and small tools.

Before working in it, read:

- `.claude/skills/jdbasic-workshop/SKILL.md` - how to work in the workshop
- `.claude/skills/jdbasic-workshop/rules.md` - jdBasic rules; read before
  writing any jdBasic

## Workspace name

The workspace is named after this project's folder: lowercase, every
character other than `a-z 0-9 _ -` replaced by `_`. Folder `My Data` gives
`my_data`, saved as `my_data.jsws` in the project root.

- Session start: `jdb_loadws name="<name>"` if `<name>.jsws` exists, then
  `jdb_funcs`.
- At every milestone and at the end: `jdb_savews name="<name>"`.

`.jsws` files hold your data. Keep them out of version control unless the
data is meant to be shared.
