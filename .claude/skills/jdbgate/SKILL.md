---
name: jdbgate
description: Run the jdBasic pre-commit gate - full test matrix (4 suites × 2 backends) plus RPG demo + emu_run smoke. Build first if needed. Report a green/red matrix; commit only if all green.
---

# jdBasic Pre-Commit Gate

Use this whenever **Atomi** is about to commit on the jdBasic repo, or asks "run the gate" / "are we green?".

Working dir: `D:\usr\dev\cc` (use forward slashes for Bash: `/d/usr/dev/cc`).

## Step 0 - decide if we need to build

Build only if `src/` is newer than `build/jdBasic.exe` or `build/jdbrt.dll`. Skip the build for `.jdb`-only changes (jdBasic interprets at runtime).

If a build is needed:

```bash
./build.bat HTTP GFX IMGUI NATIVEC 2>&1 | tail -10
./build_rt.bat HTTP GFX IMGUI NATIVEC 2>&1 | tail -3
```

Both must end with `BUILD OK`. **Always all four flags together** - dropping HTTP silently skips the HTTP slice in the test suites; dropping IMGUI strips SCREEN/RECT/SPRITE from the runtime DLL; dropping NATIVEC means `-c` won't compile.

If build fails with `LNK1104: cannot open file 'build\jdBasic.exe'`, kill the stale process directly (no need to ask): `taskkill //F //IM jdBasic.exe` - then retry the build.

If you edited `src/graphics.cpp`, `src/gui.cpp`, `src/vm_bridge.cpp`, `src/tiledmap.cpp`, or `src/imgui/*`, **`build_rt.bat` is mandatory** - `build.bat` only rebuilds the EXE; native `.exes` load the DLL.

## Step 0b - check the binary can actually run this gate (always)

Run this **before Step 1, every time**, build or no build:

```bash
FEAT=$(./build/jdBasic.exe --version 2>&1 | sed -n 's/^Features: *//p')
echo "Features: $FEAT"
MISSING=""
for f in HTTP GFX ImGui NativeC; do
  case ",${FEAT// /}," in *",$f,"*) ;; *) MISSING="$MISSING $f" ;; esac
done
if [ -n "$MISSING" ]; then
  echo "GATE ABORTED - build/jdBasic.exe lacks:$MISSING - rebuild from Step 0"
else
  echo "feature check OK"
fi
```

**If anything is missing, stop and rebuild.** Do not run the suites and do not
report a matrix - a gate on a wrong-flavour binary is worse than no gate.

The trap this closes: **the release wrappers overwrite `build/jdBasic.exe` with
their own flag set.** `build_vb6.bat` builds without `NATIVEC`, `build_mcp.bat`
without `GFX`/`IMGUI`/`NATIVEC`. After a `jdbrelease` run the binary in `build/`
is whatever the *last* wrapper produced, so every `-c` silently does nothing:
Step 1 stays fully green, Step 2 prints `COMPILE FAILED` for all four suites (or
nothing at all if you dropped the `||` branch), and the native half of the gate
was never actually exercised. Also worth knowing: a release-wrapper binary
reports a real build number instead of `Build 0, dev`, which is the other tell.

## Step 1 - interpreter pass (4 suites)

```bash
./build/jdBasic.exe tests/gate/comprehensive_test.jdb
./build/jdBasic.exe tests/gate/native_test.jdb
./build/jdBasic.exe tests/gate/test_apl_complete.jdb
./build/jdBasic.exe tests/gate/test_apl_pipelines.jdb
```

Every one must end with `ALL TESTS PASSED!` or `0 failed`. No FAIL lines, no `exit=139` (segfault).

The 4 gate suites live under `tests/gate/` along with the IMPORT helpers they need (TEST_OUTER, test_inner, native_test_modx, static_test_mod, storage, test_imp_mod). jdBasic's IMPORT walks up looking for `modules/`, but for the gate-helpers the in-dir resolution is what fires.

## Step 2 - native pass (4 suites)

The native compiler is STRICT + EXPLICIT by default. `native_test.jdb` is the
**loose** interpreter version; compile its **strict twin** `native_test.strict.jdb`
for the native pass (same 329 asserts, every var declared + typed).

**Delete the `.exe` before each `-c` and only run if it was re-produced.** A
`-c` that aborts on a STRICT/EXPLICIT compile error leaves the *previous* `.exe`
in place; running it reports a stale green and masks the breakage. (This bit us:
the untyped-DIM check broke comprehensive_test's `-c` for several commits while
the gate kept "passing" on a frozen exe.)

```bash
for t in comprehensive_test native_test.strict test_apl_complete test_apl_pipelines; do
  rm -f tests/gate/$t.exe
  ./build/jdBasic.exe -c tests/gate/$t.jdb 2>&1 | grep -iE "error at|Compiled:" | tail -2
  [ -f tests/gate/$t.exe ] && ./tests/gate/$t.exe || echo "[$t] COMPILE FAILED - not stale-green"
done
```

`jdbasic -c` auto-copies `jdbrt.dll` + `jdbasic_default.ttf` next to the produced `.exe`, so the old DLL-copy dance isn't needed here. **If a native EXE returns `exit 127`, the DLL is missing** - `cp build/jdbrt.dll tests/gate/` and retry.

**Never drop the `|| echo` branch from that loop** - without it a `-c` that
produces nothing at all prints nothing at all, and the pass reads as clean.
If *all four* report `COMPILE FAILED`, the suites are not the problem: go back
to Step 0b, the binary almost certainly has no `NATIVEC`.

## Step 3 - GUI smoke

```bash
./build/jdBasic.exe -c fluppi/rpg_demo.jdb
cp build/jdbrt.dll fluppi/jdbrt.dll
( cd fluppi && timeout 5 ./rpg_demo.exe )

./build/jdBasic.exe -c jdb/emu/emu_run.jdb
cp build/jdbrt.dll jdb/emu/jdbrt.dll
( cd jdb/emu && timeout 5 ./emu_run.exe )
```

`exit 124` from `timeout` = process killed at deadline, that's the **good** signal for graphical apps. Exit 0 / 1 also fine. **`exit 139` = segfault**, anything else likely a regression.

If the build has the FORMS flag, also run the native forms smoke - it closes
itself, so here the pass signal is **exit 0 + "ALL NATIVE FORMS TESTS PASSED!"**
(a 124 here means the event chain broke and the window never closed):

```bash
rm -f tests/forms/forms_native_smoke.exe
./build/jdBasic.exe -c tests/forms/forms_native_smoke.jdb
[ -f tests/forms/forms_native_smoke.exe ] && timeout 30 ./tests/forms/forms_native_smoke.exe
```

It needs `build_rt.bat ... FORMS` too - a DLL without the flag makes every
FORM.* call fail with "Unknown function".

## Step 3b - parity sweep (optional, ~6 min)

The four suites above are the gate. `tests/parity.sh` is the wider net: it runs
the tracked regression bank through both backends and compares each test
against `tests/parity_baseline.tsv`.

```bash
./tests/parity.sh -t 12
```

Exit 1 = at least one test got worse than the baseline; the `VS BASELINE`
block names it. Exit 0 = no regression. Run it after touching `src/vm.cpp`,
`src/llvm_codegen.cpp`, `src/jdb_runtime.cpp` or `src/vm_bridge.cpp`.

Read the `SCOPE` block before drawing conclusions:

- IMPORT helpers and the `rag`/`ai`/`http`/`tui` directories are skipped by
  default - they need models, the network, a TTY or a TUI-enabled build, and
  their reds say nothing about the backends. `--all` includes them.
- A `GAP` is usually **not** a defect. `-c` is STRICT + EXPLICIT and the
  interpreter is deliberately loose, so loose test source that the native
  compiler rejects is the two backends working as designed. What matters is
  a test that compiles and then *behaves* differently, or a new entry in the
  `VS BASELINE` block.
- Two demos (`demo_group_d`, `dupfinder`) are timeout-flaky under load and are
  recorded in the baseline at their worst state, so they can only ever report
  FIXED, never a false regression.

Once a change in the matrix is understood and intended, re-record it:
`./tests/parity.sh -t 12 --update-baseline`.

## Step 4 - report a matrix

Print a compact table to **Atomi** like this:

```
Binary: Build 0, dev - HTTP, Serial, GFX, ImGui, Forms, MiniAudio, FX, MCP, NativeC

                         interp   native
comprehensive_test         OK       OK
native_test                OK       OK
test_apl_complete          OK       OK
test_apl_pipelines         OK       OK
rpg_demo (GUI)              -       OK (timeout)
emu_run (GUI)               -       OK (timeout)

Verdict: GREEN - safe to commit.
```

Always print the `Binary:` line from Step 0b above the matrix. It is the one
piece of evidence that says the native column was produced by a compiler that
was actually built in.

Anything red → name the failing suite, paste the FAIL line(s), suggest:
1. Decode any "giant integer" PRINT - usually an f64 bit pattern indicating a tag mismatch. Check `feedback_native_int_scalar_drops_array.md` and `feedback_native_dim_init_array.md` for known shapes.
2. Re-run the failing suite alone with `--trace` if it's a native segfault.
3. **Don't commit on a partial green** - fix in `src/`, rebuild from Step 0, re-run from Step 1.

## Optional: lint changed `.jdb` files first

If the diff touches `.jdb` files, lint them via the MCP tool **before** the build (it's faster than `--lint` because the MCP-VM stays warm):

```
mcp__jdbasic-stdio-win__jdb_check path=...
```

Fall back to `./build/jdBasic.exe --lint <file>` only when the MCP tool errors out.
