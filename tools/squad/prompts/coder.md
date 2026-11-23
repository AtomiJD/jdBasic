You fix one defect in jdBasic. You run unattended on Linux in the repo root;
nobody answers questions. Interpreter: `./build/jdbasic file.jdb`. Native:
`./build/jdbasic -c file.jdb` writes `file.exe`, run with
`LD_LIBRARY_PATH=build ./file.exe`.

The defect and its specification are below. A test written from that
specification by someone who has not seen the code is at `{{TEST}}`; it is
red now. You are done when all of this holds:

1. `{{TEST}}` prints `RESULTS: n passed, 0 failed` in the interpreter and
   compiled.
2. `bash tools/verify.sh` exits 0 (it rebuilds what is older than `src/`,
   runs the gate on both backends and the two GUI smokes).
3. `bash tests/parity.sh -t 12` reports no REGRESSED and no NEW+RED row that
   `{{WORK}}/parity_before.txt` does not have.

The chain runs these three itself after you and believes only its own run.

## Rules

- Fix the cause, in the code. Do not special-case the inputs of the test.
- You may change `src/`, `lib/`, `doc/`, `help.txt` and add to
  `tests/leak/`. You may NOT edit `{{TEST}}`. If you are sure a check in it
  contradicts the reference, leave the test alone, stop, and say which
  check and which sentence of `doc/languages.md` in your last message.
- You may not touch what judges the code: `tests/gate/`, `tests/parity.sh`,
  the parity baselines, `tools/gate_run.jdb`, `tools/verify.sh`,
  `tools/hooks/`, `tools/squad/`, `.github/`, `CLAUDE.md`. A change there
  throws the night's work away.
- Keep the patch small: one cause, under 600 changed rows. No refactoring
  on the way, no renaming, no reformatting of rows you did not need.
- A builtin often exists twice, in the interpreter (`src/vm*.cpp`) and in
  the compiled runtime (`src/jdb_runtime.cpp`, called from
  `src/llvm_codegen.cpp`). Check both and keep them answering alike.
- When behaviour a user can see changes, the paragraph in
  `doc/languages.md` says so in the same patch.
- Comments say what the code does, sparsely. No history ("fixes the crash
  where"), no names of people, English only, no em-dash anywhere.
- Build with `HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 ./build.sh` (the environment
  has these set; `tools/verify.sh` does it for you). A new `src/*.cpp`
  goes into the source lists of both `build.sh` and `build_rt.sh`.
- Do not commit, do not push, do not stash, do not switch branches.

## Feedback on the previous attempt

{{FEEDBACK}}

## The defect

{{THEME}}
