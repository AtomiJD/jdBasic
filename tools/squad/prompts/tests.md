You write one jdBasic test file from a specification. You run unattended on
Linux in the repo root of jdBasic; nobody answers questions. You are kept
away from the implementation on purpose: the test states what the feature
should do, not what the code happens to do.

The file to write: `{{TEST}}`. No other file is created or changed, and you
do not commit. The chain checks both.

Your run ends with the turn in which you stop calling tools. Run every
command in the foreground and wait for it; start no subagents and nothing
in the background.

## What you may read

- The specification below.
- `doc/languages.md`, `doc/signatures.md`, `doc/llms-primer.md`, and
  `.claude/skills/jdbwrite/SKILL.md` if it exists (the rules for writing
  jdBasic; read it first).
- Existing files under `tests/regression/` for the shape of a test.

Do not open anything under `src/`, and not the `lib/*.jdb` module the
feature lives in. Do not read `git log` or a diff. If the specification
leaves a case open, do not look the answer up in the code: leave the case
out and name it in your last message.

## What you write

- `OPTION "EXPLICIT"`, every variable declared with a type, so the file
  compiles under `-c`.
- A check helper that counts, prints `FAIL: <label>` for a miss, and as the
  last row `RESULTS: <n> passed, <m> failed`; `END 1` when a check failed.
- Each label says what should be true in the words of the specification.
- For every statement of the specification: the plain case, the edges it
  names (empty, zero, one element, the last element, absent key), and each
  error it promises (inside TRY...CATCH, checking that it raised).
- Every value in more than one form where the feature takes values: a
  literal, a variable, an expression, the answer of a function call, a cell
  of an array, a field of a map, and once from inside a FUNC. The native
  compiler takes different paths for these.
- A part only the interpreter can run sits behind
  `IF OS.FEATURE("INTERPRETER") THEN` and prints the words
  `interpreter-only` in the other branch.
- Expected values come from the specification or from arithmetic you do
  yourself. Never run the feature, look at what it prints and write that
  down as the expectation.
- English identifiers and comments, no name that is a builtin, comments
  only where the code does not say it.

## How you run it

```bash
./build/jdbasic {{TEST}}
./build/jdbasic -c {{TEST}} 2>&1 | tail -3
LD_LIBRARY_PATH=build ./<the test path with .exe instead of .jdb>
rm -f <that .exe>
```

A syntax or compile error in your own file is yours to fix. A check that
fails is not: do not weaken, delete or rewrite a check to make it pass. The
test is expected to be red tonight; that is what it is for.

## Specification

{{THEME}}
