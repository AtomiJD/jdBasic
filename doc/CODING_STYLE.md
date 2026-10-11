# Coding Style

Binding conventions for `src/` (C++) and for `fluppi/`, `tests/`, `jdb/`
(jdBasic). The goal is a single point of truth: the code says WHAT it does,
and a comment is there only when the WHY is not obvious.

## C++ (src/)

### Formatting
- Four-space indent, no tabs
- LF line endings in the repository. Nothing in `.gitattributes` enforces that
  for `src/`; it is `core.autocrlf` that normalises on commit, so a Windows
  checkout holds CRLF and that is expected.
- Opening brace on the same line: `if (x) {`
- About 100 characters a line, a soft limit - readability beats the number

### Naming
- `snake_case` for functions, variables, fields and methods
- `PascalCase` for types: `struct`, `class`, `enum class`
- `ALL_CAPS` only for macros and compile-time constants (`JD_TAG_*`)
- No `m_` prefix on private fields, except for transient codegen state
  (`m_want_leaf_tag`)
- `pragma once` in headers, no include guard

### Types
- `enum class` over a plain `enum`. A plain enum only where C interop needs it,
  and then with parallel `#define` aliases such as `JD_TAG_*`
- No magic numbers for tagged values, always the named constant from the
  central header
- `const` wherever it fits: parameters, locals, methods
- Brace initialisation for struct literals: `{ val, JD_TAG_F64 }` rather than
  `TypedValue(val, 1)`

### Comments
**The default is no comment.** Write one only when:
1. there is a hidden invariant the code does not show by itself,
2. it is a workaround for a specific defect (briefly: what and why),
3. a constant is surprising, or correctness is not obvious,
4. it is public API in a header: one line on semantics and ownership.

**Do not write:**
- a narrative of what the code does (`// Convert index to i64` right above
  `LLVMBuildFPToSI`)
- history (`// used to be X, now Y`): that is what the git log is for
- phase markers (`// Phase 3b:`): phase history belongs in a commit message
- section banners (`// == PLAYER STATS ==`): a file that needs structured
  sections wants splitting instead

**When you do write one:**
- one line preferred, a block only where it is really needed
- it says WHY, not WHAT
- a comment carrying a date, a ticket or "see issue #123" stays; that is
  context the code does not hold

### Error handling
- `report_error(file, line, msg)` collects diagnostics: everything the pass
  found, not the first failure
- No silent failures. Where a dispatch decision is unexpected, throw or
  report it.

### Tag system
- Every tag literal comes from `JD_TAG_*` in `jdb_tags.h`
- `VarInfo.tag`, `TypedValue.tag`, `return_tag`, `param_tags[i]` always use the
  macro
- `// JdTag (see jdb_tags.h)` on the field is comment enough

## jdBasic (fluppi/, tests/, jdb/)

### Compiler targets
- `OPTION "EXPLICIT"` and `OPTION "STRICT"` at the top of the file, both
  mandatory
- Every variable declared with `DIM` or initialised with `LET`
- `AS <type>` on every `DIM` whose type is not unambiguous from the initialiser

### Interpreter-only scripts
- May use bare assignment (the loose mode)
- A script that runs interpreted but may also be compiled is a compiler target
  and follows those rules

### Naming
- `snake_case` throughout. jdBasic is case-insensitive, but a consistent
  spelling is what makes `grep` useful.
- `$` suffix on string variables
- Module names as `SUB_AREA.NAME`, for example `RPG_DIALOG.SAY`
- Never a builtin's name. All three paths refuse it, `--lint` included.

### Structure
- Imports at the top, after `EXPORT MODULE`
- Public API marked with `EXPORT`
- No whole-file comment banners beyond the header block at the very top
  (`' == Module Title ==`)

## Commits
- One stable milestone, one commit
- Subject in the imperative, under 72 characters
- The body says WHY - the diff already shows what - and which tests ran
- No `Co-Authored-By` trailer
- A reference to the ticket where there is one

## Tests
- Every new language feature gets a regression test, written before the fix
- A new keyword is entered in `doc/languages.md`, `help.txt` (repository root)
  and `syntaxes/jdbasic.tmLanguage.json`
- After a compiler change, run the gate: the four suites under `tests/gate`
  through both backends, then the two GUI smokes (`fluppi/rpg_demo.jdb` and
  `jdb/emu/emu_run.jdb`), where `exit 124` from `timeout` is the pass
