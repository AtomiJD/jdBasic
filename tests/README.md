# jdBasic Test Suite

Roughly **284 tracked tests** covering language semantics, native codegen, APL primitives, GUI / TUI, FFI, modules, async, and per-feature corner cases. The directory is part regression bank, part scratch-pad — see the naming conventions below to tell them apart.

---

## ✅ Pre-commit gate (5 suites × 2 backends)

Every commit must pass the gate from **both** the interpreter and the LLVM native compiler. Run the sequence below; each command must end with `ALL TESTS PASSED!` (or `0 failed`).

| Suite | What it covers | Asserts |
|---|---|---|
| `comprehensive_test.jdb` | Loops, variables, types, FUNCs / SUBs, lambdas, pipes, react, strings, math, operators, Unicode — the big mixed bag (51 sections). | 705 |
| `native_test.jdb` | Subset known to lower through native codegen. Catches IR / runtime divergence. | 265 |
| `test_apl_complete.jdb` | APL-style array primitives: `IOTA`, `RESHAPE`, `GRADE`, `OUTER`, `CONVOLVE`, `MATMUL`, etc. | 143 |
| `test_apl_pipelines.jdb` | Multi-stage `\|>` pipe expressions exercising tag inference + auto-vectorization. | 52 |
| `test_tui_smoke.jdb` | Every `TUI.*` native — symbol presence + return-value preservation. | 21 |

### Interpreter pass

```bash
./build/jdBasic.exe tests/comprehensive_test.jdb
./build/jdBasic.exe tests/native_test.jdb
./build/jdBasic.exe tests/test_apl_complete.jdb
./build/jdBasic.exe tests/test_apl_pipelines.jdb
./build/jdBasic.exe tests/test_tui_smoke.jdb
```

### Native pass

`jdbasic -c` auto-copies `jdbrt.dll` next to the produced `.exe` (since 2026-04-23), so no manual DLL dance:

```bash
./build/jdBasic.exe -c tests/comprehensive_test.jdb && ./tests/comprehensive_test.exe
./build/jdBasic.exe -c tests/native_test.jdb        && ./tests/native_test.exe
./build/jdBasic.exe -c tests/test_apl_complete.jdb  && ./tests/test_apl_complete.exe
./build/jdBasic.exe -c tests/test_apl_pipelines.jdb && ./tests/test_apl_pipelines.exe
./build/jdBasic.exe -c tests/test_tui_smoke.jdb     && ./tests/test_tui_smoke.exe
```

If a native EXE returns **exit 127**, the DLL is missing next to it — `cp build/jdbrt.dll tests/` and retry.

### GUI smokes

Graphical apps are run native with a 5-second budget; exit code **124** (timeout-killed) is the **good** signal:

```bash
./build/jdBasic.exe -c fluppi/rpg_demo.jdb
cp build/jdbrt.dll fluppi/
( cd fluppi && timeout 5 ./rpg_demo.exe )

./build/jdBasic.exe -c jdb/emu_run.jdb
cp build/jdbrt.dll jdb/
( cd jdb && timeout 5 ./emu_run.exe )
```

The full procedure (including how to react to LNK1104 build locks and giant-integer prints) lives in `.claude/skills/jdbgate/SKILL.md`.

---

## 📚 Naming conventions

| Shape | Purpose |
|---|---|
| `test_<feature>.jdb` | Single-feature focused test |
| `test_<feature>_<v>.jdb` | Variant of the above (numbered or `a/b/c`) |
| `*_smoke.jdb` | Minimal "imports / runs / exits clean" check |
| `comprehensive_test.jdb` | The mega-suite — 705 asserts in 51 sections |
| `native_test.jdb` | Native-codegen-focused mirror of comprehensive |
| `test_apl_*.jdb` | APL array primitives (the canonical guard since the great native audit) |
| `test_tui_phase_*.jdb` | One file per TUI.* implementation phase (A..G) — useful as feature demos, not part of the gate |
| `crash_test.jdb` | Stress / fuzz suite, catches segfault classes |
| `demo_group_<a-d>.jdb` | Curated demo bundles for the website |
| `TEST_INNER.jdb` / `TEST_OUTER.jdb` | Module-loading pair (LOAD-inside-LOAD) |

Self-checking shape every test follows:

```basic
DIM PASS AS INTEGER = 0
DIM FAIL AS INTEGER = 0

SUB Assert(cond, msg)
    IF cond THEN PASS = PASS + 1
    ELSE
        FAIL = FAIL + 1
        PRINT "FAIL: "; msg
    ENDIF
ENDSUB

' ... tests ...

PRINT "RESULTS: "; PASS; " passed, "; FAIL; " failed"
IF FAIL = 0 THEN PRINT "ALL TESTS PASSED!"
```

The gate harness greps for `ALL TESTS PASSED!` — a test that doesn't print that line counts as failed.

---

## 🗂  Categories (file counts)

Roughly grouped by the `test_<bucket>_*` prefix:

| Bucket | Count | Topic |
|---|---|---|
| `eval`        | 11 | EVAL / EXECUTE statement variants |
| `tui_phase`   |  7 | TUI.* per-phase smokes (A..G) |
| `npc`         |  6 | RPG NPC initialization |
| `rag`         |  6 | Retrieval-augmented generation |
| `native`      |  6 | Native codegen specifics |
| `native_wire` |  5 | `vm_bridge` wire format |
| `json`        |  5 | JSON parse/stringify |
| `func_map`    |  5 | Function references stored in MAPs |
| `react`       |  4 | Reactive bindings |
| `for_step`    |  4 | `FOR ... STEP` semantics |
| `demo_group`  |  4 | Curated demos |
| `apl`         |  4 | APL-style array primitives |
| `udt_*`       |  4 | `INIT` / `DISPOSE` lifecycle (`udt_init`, `udt_dispose`) |
| `http`        |  3 | HTTP server / client |
| `xmod`        |  2 | Cross-module function calls |
| `strict*`     |  6 | `STRICT` / `EXPLICIT` enforcement |
| `pdf`         |  2 | PDF generation (libharu) |
| `props`       |  2 | UDT field access |
| `member`      |  3 | Member-access patterns |

Full breakdown:

```bash
git ls-files tests/test_*.jdb \
  | sed 's|tests/test_||' \
  | awk -F_ '{print $1}' \
  | sort | uniq -c | sort -rn
```

---

## 🎮 Special tests + GUI smokes

These don't fit the self-checking pattern but are part of the canonical gate:

| File | Mode | Notes |
|---|---|---|
| `fluppi/rpg_demo.jdb` | native + timeout 5 | Tiled-driven RPG; exit 124 = good |
| `jdb/emu_run.jdb` | native + timeout 5 | Apple II framebuffer + 6502 core |
| `tests/test_tui_smoke.jdb` | interp + native | TUI.* native presence + return values (no real terminal) |

---

## ➕ Adding a new test

1. **Extend first, create second.** If your test exercises a shipped feature comprehensively, **add asserts to `comprehensive_test.jdb` or `native_test.jdb`** instead of creating a new file. Only spin up a new top-level file when:
   * It's a new feature area with **≥5 distinct assertions**
   * It must run isolated (different `OPTION` flags, separate VM state)
2. Use the self-checking shape (PASS/FAIL counters + final `ALL TESTS PASSED!` line). The gate harness automatically picks it up.
3. **Run both backends** — interp green and native green. Many bugs hide on exactly one side; see `feedback_native_int_scalar_drops_array.md` for a known class.
4. **Throwaway debug repros** from a session go to `_quarantine/` and use the `_` prefix (auto-ignored by `.gitignore`). See `move_proposal.txt` in the repo root for the curation script.

---

## 📎 See also

* **`doc/languages.md`** — Language reference
* **`help.txt`** — Per-keyword help (loaded by `HELP <topic>`)
* **`.claude/skills/jdbgate/SKILL.md`** — Detailed gate procedure for AI-assisted sessions
* **`.claude/skills/jdbtest/SKILL.md`** — Single-file end-to-end verify procedure
