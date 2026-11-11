---
name: jdbgate
description: Run the jdBasic pre-commit gate - full test matrix (4 suites × 2 backends) plus RPG demo + emu_run smoke. Build first if needed. Report a green/red matrix; commit only if all green.
---

# jdBasic Pre-Commit Gate

Use this whenever **Atomi** is about to commit on the jdBasic repo, or asks "run the gate" / "are we green?".

Working dir: `D:\usr\dev\cc` (use forward slashes for Bash: `/d/usr/dev/cc`).

## The short way - one command

```bash
./tools/verify.sh            # add --parity for Step 3b, --build to force a build
```

It does Steps 0 to 3 below: rebuilds `build/jdBasic.exe` and
`build/jdbrt.dll` each when a source under `src/` is newer (flags from
`JDB_BUILD_FLAGS`, default the base set with MCPSERVER), prints the
`Features:` line, then runs `tools/gate_run.jdb --smokes`: a `binary` step
(`STALE` = a source is newer than a binary), the four suites on both
backends and the two GUI smokes, each ended by the gate after 5 s. One
`GATE {...}` line per step; exit 0 = green, 1 = a step failed, 2 = the build
failed. A green run stamps `.claude/.tests_ok`, so a commit right after it
passes the hook. It does not kill a `jdBasic.exe` that locks the build; do
that as Step 0 says and run it again. Still check the `VERIFY features:` line
against Step 0b, and report the matrix of Step 4 from the GATE lines.

The steps below are what it does, for running a part by hand.

## Step 0 - decide if we need to build

Build only if `src/` is newer than `build/jdBasic.exe` or `build/jdbrt.dll`. Skip the build for `.jdb`-only changes (jdBasic interprets at runtime).

If a build is needed:

```bash
./build.bat HTTP NET COM GFX IMGUI NATIVEC 2>&1 | tail -10
./build_rt.bat HTTP NET COM GFX IMGUI NATIVEC 2>&1 | tail -3
```

Both must end with `BUILD OK`. **Always all six flags together** - COM is part of every Windows build because the VBA customer work needs it; dropping HTTP silently skips the HTTP slice in the test suites; dropping NET turns `tests/net/net_test.jdb` red in the parity sweep; dropping IMGUI strips SCREEN/RECT/SPRITE from the runtime DLL; dropping NATIVEC means `-c` won't compile.

If build fails with `LNK1104: cannot open file 'build\jdBasic.exe'`, kill the stale process directly (no need to ask): `taskkill //F //IM jdBasic.exe` - then retry the build.

If you edited anything under `src/` that a compiled program reaches at
runtime, **`build_rt.bat` is mandatory** - `build.bat` only rebuilds the
EXE, and native `.exes` load the DLL. That includes `src/vm.cpp` and the builtins in
`src/vm_builtins_*.cpp`, which is the one that catches people: the DLL carries the VM, so a builtin
fixed there is right in the interpreter and stale in every generated EXE
until the DLL is rebuilt. Symptom: Step 1 green, Step 2 red, on a change
that is correct.

## Step 0b - check the binary can actually run this gate (always)

Run this **before Step 1, every time**, build or no build:

```bash
FEAT=$(./build/jdBasic.exe --version 2>&1 | sed -n 's/^Features: *//p')
echo "Features: $FEAT"
MISSING=""
for f in HTTP NET COM GFX ImGui NativeC; do
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

**Steps 1 + 2 in one command:** `./build/jdBasic.exe tools/gate_run.jdb`
runs the same eight runs (deletes each `.exe` first, judges a missing RESULTS
line as CRASH) and prints one `GATE {...}` JSON line per step, exit 0 = green;
`--parity` appends Step 3b. Across all farm machines at once:
`tools/farm_gate.jdb` (see the jdbfarm skill).

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

**Sweep up after both smokes** - `timeout` reports 124 but does not reliably kill a native GUI process on Windows, so the window can survive the gate and keep running:

```bash
taskkill //F //IM rpg_demo.exe 2>/dev/null; taskkill //F //IM emu_run.exe 2>/dev/null; true
```

An orphan matters here because a compiled GUI program grows by roughly 40 MB
per second (trakr jdbasic #365), so one left behind reaches tens of gigabytes
within the hour.

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

The GFX capture smoke closes itself the same way, and the pass signal is
**exit 0 + "ALL GFX CAPTURE TESTS PASSED!"**. It checks what an image id is
worth: the kind `GFX.CAPTURE` answers, that two captures answer two ids, and
that each one draws and frees. The sweep skips `tests/gfx/` for the same
reason it skips `tests/forms/`, so this is where it gets run:

```bash
rm -f tests/gfx/gfx_capture_smoke.exe
./build/jdBasic.exe -c tests/gfx/gfx_capture_smoke.jdb
[ -f tests/gfx/gfx_capture_smoke.exe ] && timeout 25 ./tests/gfx/gfx_capture_smoke.exe
```

The font cache smoke works the same way (exit 0 + "ALL FONT CACHE TESTS
PASSED!"): sizes measure the same after switching back and forth, both
caches empty themselves when full, and a second `SCREEN` draws text without
the textures of the renderer it replaced. Run it from `tests/gfx`, where the
default font sits:

```bash
rm -f tests/gfx/font_cache_smoke.exe
./build/jdBasic.exe -c tests/gfx/font_cache_smoke.jdb
[ -f tests/gfx/font_cache_smoke.exe ] && ( cd tests/gfx && timeout 25 ./font_cache_smoke.exe )
```

The smooth smoke (exit 0 + "ALL NEWTARGET SMOOTH TESTS PASSED!") checks that
`GFX.NEWTARGET(w, h, FALSE)` keeps a hard pixel edge when drawn larger and
that the default still filters:

```bash
rm -f tests/gfx/newtarget_smooth_smoke.exe
./build/jdBasic.exe -c tests/gfx/newtarget_smooth_smoke.jdb
[ -f tests/gfx/newtarget_smooth_smoke.exe ] && ( cd tests/gfx && timeout 25 ./newtarget_smooth_smoke.exe )
```

## Step 3a - leak check (~6 s)

Run it after touching `src/llvm_codegen.cpp`, `src/jdb_runtime.cpp` or
`src/vm_bridge.cpp`:

```bash
./tests/leak_check.sh
```

It runs each pattern of `tests/leak/leak_patterns.jdb` in both backends and
reads `SYS.HEAP()` after a third, two thirds and all of the passes. A pattern
whose heap grows in both thirds leaks. Exit 1 = a `NEW-LEAK` that
`tests/leak/known_leaks.txt` does not list. A `FIXED` line is a listed leak
that went flat: take it off the list in the same commit. A new memory
pattern worth guarding goes into `leak_patterns.jdb` as one more
`ELSEIF pattern$ = "..."` branch; the script finds it by that line.

## Step 3b - parity sweep (optional, ~90 s with the default 8 parallel jobs)

The four suites above are the gate. `tests/parity.sh` is the wider net: it runs
the tracked regression bank through both backends and compares each test
against `tests/parity_baseline.tsv`.

```bash
./tests/parity.sh -t 12
```

Exit 1 = at least one test got worse than the baseline; the `VS BASELINE`
block names it. Exit 0 = no regression. Run it after touching `src/vm.cpp`, `src/vm_builtins_*.cpp`,
`src/llvm_codegen.cpp`, `src/jdb_runtime.cpp` or `src/vm_bridge.cpp`.

The default sweep leaves out the tests that open the network (HTTP, NET,
sockets): every sweep compiles into a fresh folder, each exe is new to the
Windows firewall, and its prompt stalls an unattended run. Run that part
separately, only while Atomi is at the machine to answer the prompts:

```bash
./tests/parity.sh -t 12 --net
```

Never start a second sweep while one is still running, and do not build while
one runs: a build removes `build/jdBasic.exe` for a moment and every test
started in that window reads as red.

Read the `SCOPE` block before drawing conclusions:

- IMPORT helpers and the `rag`/`ai`/`http`/`tui` directories are skipped by
  default - they need models, the network, a TTY or a TUI-enabled build, and
  their reds say nothing about the backends. `--all` includes them.
- Since 2026-11-04 the sweep also compares **output**: both backends green
  but printing something else is `DIVERGE` (ranked like a GAP, listed with
  the gaps), unless the interpreter disagrees with itself on a second run
  (timings, random numbers), which is `NONDET` and ranks like OK. The
  baseline of that day records 30 DIVERGE already in the bank - some by
  design (comprehensive_test skips interpreter-only sections natively),
  most real (test_math IIF over an array, test_slice, test_type UDT print).
  A test moving from OK to DIVERGE is a regression like any other.
- A `GAP` **is** a defect candidate since 2026-10-24: the interpreter is green,
  the compiled program is red, and nothing explains it. The two by-design
  disagreements have their own verdicts and sit in the summary, not in the
  GAP list: `LOOSE` (`-c` is STRICT + EXPLICIT and refused loose test source;
  the script checks that the named variable really has no declaration) and
  `UNSUPPORTED` (the compiler, the runtime or the test itself said
  "interpreter-only": HELP, SQL.QUERY, GROUPBY with a funcref, FOR EACH over
  a channel, EXECUTE semantics). A test that skips its native half does so
  through `OS.FEATURE("INTERPRETER")` and prints the words `interpreter-only`.
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
