---
name: vbaedit
description: Edit VBA source files (.bas/.cls/.frm) byte-safely via the jdBasic VBA module. Loads CP1252, modifies as UTF-8 with normal jdBasic string ops, saves back as CP1252 - the encoding Access/Excel re-imports cleanly. Use whenever Atomi asks to edit, refactor, search, or generate VBA code.
---

# VBA Source Editing

Use this whenever **Atomi** asks to touch a `.bas` / `.cls` / `.frm` file from his Access / Excel projects. The Edit/Write tool stack writes UTF-8 and **destroys CP1252 umlauts** (`ä` becomes `?`, the file gets corrupted on the next VBA import). This skill routes everything through jdBasic's MCP server, which now handles CP1252 round-trip cleanly.

## Prerequisite

Atomi's MCP server (`jdbasic-stdio-win`) must be running. Tools live under `mcp__jdbasic-stdio-win__*`. If they're not in your tool list, ask Atomi to enable the MCP server.

## The full toolkit

| Step | Tool | Purpose |
|------|------|---------|
| Load | `jdb_eval` with `IMPORT VBA : DIM s$ = VBA.LOAD$("<path>")` | Read .bas file, decoded to UTF-8 in `s$` |
| Inspect | `jdb_eval` with `PRINT INSTR(s$, "...")`, `LEN(s$)`, `MID$(s$, ...)` | Locate / extract code |
| Modify | `jdb_eval` with `s$ = REPLACE$(s$, old, new)` etc. | Manipulate as UTF-8 |
| Save | `jdb_eval` with `VBA.SAVE "<path>", s$` | Write back as CP1252 |

The `s$` variable persists across `jdb_eval` calls in the MCP session - **don't re-load** the file between operations. Atomi pointed out this is the whole reason for the workflow: one LOAD$, many edits, one SAVE.

## Canonical session

```
1.  IMPORT VBA
    DIM s$ AS STRING = VBA.LOAD$("D:\some\path\modFoo.bas")
    PRINT LEN(s$)                 ' sanity-check the load worked

2.  PRINT INSTR(s$, "ProcedureName")     ' find a target
    PRINT MID$(s$, 1234, 200)            ' extract a chunk to read

3.  s$ = REPLACE$(s$, "Old code", "New code")
    s$ = REPLACE$(s$, "Datensätze", "Datensätze (geprüft)")
    ' Umlauts in literals work because s$ is UTF-8 internally

4.  VBA.SAVE "D:\some\path\modFoo.bas", s$
```

## Important behaviours

- **`MID$` is 0-indexed** in jdBasic, not 1-indexed like classic BASIC. `MID$(s$, 0, 5)` returns the first 5 chars.
- **`INSTR(haystack$, needle$)`** returns the position when found, **`-1` (or sometimes `0`) when not found** - treat any value `< 1` as miss. Don't rely on `= 0` to detect "missing".
- **`LEN(s$)`** returns byte count of the UTF-8 form, so `LEN` for an umlaut-rich CP1252 file is slightly larger than its on-disk size (each umlaut is 1 byte CP1252 → 2 bytes UTF-8).
- **Don't mix encodings**: never feed an `s$` that came from `TXTREADER$(path)` (no encoding arg, raw bytes) into `VBA.SAVE` - you'll double-encode.
- **CRLF line endings** are preserved through the round-trip (binary mode read/write).

## Verifying the result

After SAVE, ask Atomi to re-import the file in Access/Excel, OR run a quick check from the shell:

```bash
file modFoo.bas              # should say "ISO-8859 text, with CRLF line terminators"
python -c "import sys; d=open('modFoo.bas','rb').read(); print('high bytes:', sum(1 for b in d if b>=0x80))"
```

The high-byte count must match the original (counted before LOAD$).

## When NOT to use this

- New files created from scratch with **only ASCII characters** - TXTWRITER without encoding works fine, no need for the cp1252 dance.
- VBA code blocks that Atomi pastes into the chat for review (no file involved).

## Failure modes

- `Cannot open file: <path>` → check the path. Forward / back slashes both OK on Windows; spaces need quotes around the literal.
- The output file looks like UTF-8 mojibake in VBA → you wrote without the `"cp1252"` arg, or read with TXTREADER$ instead of VBA.LOAD$. Re-do via the module wrappers.
- Imports broken with garbled umlauts → the source `s$` was already corrupted before SAVE. Decode the original again with `VBA.LOAD$` and start over.

## Module location

Two equivalent paths:

1. **`D:\usr\dev\cc\vba\vba.jdb`** - module form. Use after `IMPORT VBA` then `VBA.LOAD$ / VBA.SAVE / VBA.APPEND_TO`. The cwd must be in (or relative to) the `vba/` folder for IMPORT to find the file.
2. **`<project>/VBASTACK.jdws`** - workspace form. Use after `mcp__jdbasic-stdio-win__jdb_loadws name="VBASTACK"`. Top-level free funcs `VBA_LOAD$ / VBA_SAVE / VBA_APPEND_TO` (underscore, no dot). The BAUMAX project has one of these - check the project's `CLAUDE.md` for whether to use IMPORT or LOADWS.

If Atomi wants more helpers (e.g. `LIST_PROCS$`, `EXTRACT_PROC$`), define them once in the MCP session and `jdb_savews name="VBASTACK"` to persist into the project workspace.
