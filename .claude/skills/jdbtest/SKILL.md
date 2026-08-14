---
name: jdbtest
description: Run the 5-point test matrix on a single .jdb file - lint, interp, native compile, native run, plus a `--lint` fallback. Use when Atomi asks to "test this file" or "matrix on X.jdb". The file path comes from the slash-command argument or the most recent context.
---

# Single-File Test Matrix

Use when **Atomi** asks to verify a specific `.jdb` file end-to-end. Working dir: `/d/usr/dev/cc`.

The target file is whatever Atomi passed after `/jdbtest` (or named in the message). If unclear, ask which file.

## Step 1 - Lint via MCP

Prefer the MCP tool over `--lint` (the persistent VM keeps the parser cache warm):

```
mcp__jdbasic-stdio-win__jdb_check path=<file>
```

Fall back to `./build/jdBasic.exe --lint <file>` only if the MCP tool errors out. **Stop and report** if lint fails - no point running a broken file.

## Step 2 - Interpreter run

```bash
./build/jdBasic.exe <file>
```

Look for:
- `ALL TESTS PASSED!` / `0 failed` if it's a self-checking test file
- Clean exit (no `exit=139`) for a demo
- Stderr clear of `Undefined function` / `Type mismatch`

## Step 3 - Native compile

```bash
./build/jdBasic.exe -c <file>
```

Compile produces `<file_basename>.exe` in the same directory; `jdbrt.dll` is auto-copied next to it (since 2026-04-23). On compile error, print the first compiler diagnostic and stop - STRICT/EXPLICIT errors usually point at a missing `AS Type` annotation. See `feedback_native_dim_init_array.md` for the typical "DIM x" → i64-slot trap.

## Step 4 - Native run

```bash
( cd <dir-of-file> && ./<basename>.exe )
```

Or for GUI programs:
```bash
( cd <dir-of-file> && timeout 5 ./<basename>.exe )
```

Exit codes:
- `0` / `1` = normal exit, fine
- `124` = timeout-killed (good for GUI / loop demos)
- `127` = `jdbrt.dll` not next to the EXE → `cp build/jdbrt.dll <dir>/` and retry
- `139` = segfault → grab the last PRINT before death and decode any "giant integer" as f64 bits

## Step 5 - Compare interp vs native

If both ran self-checking asserts, the assert counts must match. A divergence means a native codegen bug - common shapes:
- `arr × i64 + i64` collapse → `feedback_native_int_scalar_drops_array.md`
- Bare `DIM x` then `x = matrix-expr` → `feedback_native_dim_init_array.md`
- Tag drift in pipe expression → check `infer_tag` recently touched

## Step 6 - Report a compact result

```
File: <file>
  lint    OK
  interp  OK   (143/143 asserts)
  -c      OK
  native  OK   (143/143 asserts)
  parity  OK
```

Or red lines with the failing diagnostic. Do NOT bury a failure in a green-looking summary.
