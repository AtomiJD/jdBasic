# Contributing to jdBasic

Thanks for your interest! Bug reports, feature ideas, docs fixes and code are all welcome.

## Building

See [doc/BUILD.md](doc/BUILD.md) for the full guide. The short version: a **core build needs no third-party binaries at all** - MSVC plus the header-only Eigen library under `libs/eigen` is enough (this is exactly what CI does). Feature flags (`GFX`, `IMGUI`, `HTTP`, `NATIVEC`, `FORMS`, `SQLITE`, ...) pull in the libraries listed in BUILD.md.

## The gate

Every change must keep the pre-commit gate green. The four suites live in `tests/gate/`:

```bash
./build/jdBasic.exe tests/gate/comprehensive_test.jdb
./build/jdBasic.exe tests/gate/native_test.jdb
./build/jdBasic.exe tests/gate/test_apl_complete.jdb
./build/jdBasic.exe tests/gate/test_apl_pipelines.jdb
```

All four must report `0 failed`. If your change touches the native compiler (`src/llvm_codegen.cpp`, `src/jdb_runtime.cpp`, `src/vm_bridge.cpp`), also run the native pass - compile each suite with `-c` and run the produced `.exe`. Two things to know:

- The native compiler enforces **STRICT + EXPLICIT**; the loose `native_test.jdb` is interpreter-only and `native_test.strict.jdb` is its native twin. An interpreter-green / native-red *type* error is often working as designed - see [tests/README.md](tests/README.md).
- Delete stale `.exe`s before a `-c` run: a compile error leaves the previous binary in place, which would report a stale green.

GUI-affecting changes should also pass the GUI smoke (see tests/README.md): the RPG demo and the emulator front-end must still build and start.

CI runs the core build plus the interpreter gate on every push and pull request. It cannot run the GFX/native paths (no vendored libs), so a green check is necessary but not sufficient for runtime/codegen changes - run the full gate locally.

## Commit conventions

The history uses [Conventional Commits](https://www.conventionalcommits.org/): `feat(scope): ...`, `fix(scope): ...`, `docs: ...`, `perf: ...`, `refactor: ...`. Two local conventions:

- **`[no-test]`** at the end of the subject marks commits where the gate was not run because it does not apply (docs, demo assets, website text). Code changes never carry it.
- Comments describe *what the code does*, never the change history ("fixed the bug where...") - that belongs in the commit message.

## Code style

- C++ core: see [doc/CODING_STYLE.md](doc/CODING_STYLE.md).
- Identifiers and comments are **English-only**, in `.jdb` samples too.
- New keywords or builtins must be registered in `doc/languages.md`, `help.txt` - the language reference is also read at runtime (`HELP`, the MCP `jdb_doc` tool, editor hovers), so an undocumented builtin effectively does not exist. The TextMate grammar lives in the VS Code extension repository; `tools/update_syntax_highlighting.py` regenerates a drop-in copy from `src/`.

## Reporting bugs

The perfect bug report is a **minimal `.jdb` repro** plus:

- the output of `jdBasic.exe --version` (build number and feature list),
- whether it happens in the interpreter, the native compiler (`-c`), or both,
- what you expected vs. what happened.

## Pull requests

Keep them small and focused. Say in the description which parts of the gate you ran and their results. For anything ambitious, consider opening a discussion or issue first so we can talk design before you invest serious time.
