# jdBasic for Visual Studio Code (v1.0.19)

The `jdbasic-debug` extension turns VS Code into a small IDE for jdBasic: a
full debugger plus editor language features (linting, autocomplete, hover
docs, signature help, go-to-definition and an outline). All language features
talk to the jdBasic runtime itself, so they never drift from the real build.

## 1. Prerequisites

1. **Visual Studio Code**, up to date.
2. **jdBasic.exe** (the interpreter/runtime). For the debugger you can rely on
   `PATH`, but for the editor features point the extension at a current build
   (see Settings below). The editor features need a build new enough to
   support `--lint`, `--dump-symbols` and `--dump-help`.

## 2. Installation

1. Extensions view (`Ctrl+Shift+X`) -> `...` menu -> **Install from VSIX...**.
2. Pick `jdbasic-debug-1.0.19.vsix`.
3. **Reload Window** when prompted.

## 3. Settings

Open Settings (`Ctrl+,`) and search for `jdbasic`, or edit `settings.json`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `jdbasic.runtime` | `jdBasic.exe` | Path to the runtime used for linting, autocomplete and hover. A bare name resolves via `PATH`; set a full path to pin a build, e.g. `D:/usr/dev/cc/build/jdBasic.exe`. |
| `jdbasic.lint.enable` | `true` | Show parse/compile errors and undeclared-reference warnings as diagnostics. |
| `jdbasic.lint.onType` | `true` | Re-lint while typing (debounced). When off, lint only on open and save. |

> Tip: if completions or hovers are empty, your `jdbasic.runtime` is probably
> an older build without the dump flags. Point it at a current `jdBasic.exe`.

## 4. Editor features (no debug session needed)

These work as soon as you open a `.jdb` file.

- **Linting / diagnostics**: parse errors and interpreter compile-check errors
  (undefined GOTO labels, `STATIC DIM` misuse, `DIM` shadowing a parameter,
  `TYPE` constructor args without `SUB INIT`, ...) show as red squiggles;
  undeclared references show as yellow warnings. Errors also appear in the
  Problems panel (`Ctrl+Shift+M`). Diagnostics come from `jdBasic --lint`.
- **Autocomplete**: keywords, all built-in functions, namespaced methods
  (after typing `GFX.`), constants, and the `FUNC`/`SUB`/`DIM`/`LET` names from
  the current file. Sourced from `jdBasic --dump-symbols`.
- **Hover docs**: hover a function or keyword to see its syntax and
  description (from `jdBasic --dump-help`).
- **Signature help**: typing `LEFT$(` shows the parameters; the active
  parameter is highlighted as you type past each comma.
- **Go to Definition**: jump to a `FUNC`/`SUB`/`TYPE` definition or a
  `DIM`/`LET` declaration.
- **Outline / symbols**: `FUNC`/`SUB`/`TYPE` appear in the Outline view,
  breadcrumbs, and the Go-to-Symbol list.

## 5. Debugging

### Launch configuration

Run and Debug view (`Ctrl+Shift+D`) -> create a `launch.json`, choose
**jdBasic Debug**. Example:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "jdbasic",
      "request": "launch",
      "name": "jdBasic: Debug in Terminal",
      "program": "${file}",
      "stopOnEntry": true,
      "runtime": "D:/usr/dev/cc/build/jdBasic.exe",
      "console": "integratedTerminal"
    }
  ]
}
```

`stopOnEntry: false` runs straight to your first breakpoint instead of
pausing on the first line.

### Breakpoints

Click the gutter (or `F9`) to set a breakpoint. Right-click a breakpoint ->
**Edit Breakpoint** to add:

- **Expression** (conditional): pause only when it is true, e.g. `i = 4`
  (jdBasic uses a single `=` for equality, not `==`).
- **Hit Count**: e.g. `3`, `>5`, `%4`.
- **Log Message** (logpoint): logs without stopping; `{expr}` parts are
  evaluated, e.g. `loop i={i}`.

### Inspecting state

- The **Variables** panel shows the selected stack frame's **Locals** plus
  **Globals**. Arrays, maps and UDTs are **expandable** (drill into elements
  and fields).
- **Hover** over a variable or expression in the editor to see its value
  (handles `arr[2]`, `obj.field` and computed expressions).
- **Watch** panel: add any expression; array/map results are expandable.
- Right-click a variable for **Copy Value** / **Add to Watch**.

### Debug Console (REPL)

While paused, the Debug Console runs full jdBasic, not just expressions:

```
PRINT 6 * 7
x = 42
FOR k = 1 TO 3 : PRINT k : NEXT k
```

Plain expressions print their value (`counter`, `x + 1`).

### Edit while debugging (recompile on save)

While paused you can edit the source and **save** (`Ctrl+S`). The running
program is recompiled and the instruction pointer is repositioned to the same
line, so execution continues in place.

### Break on error

Any jdBasic runtime error pauses execution at the offending line and shows the
error details. This is always on.

## 6. Keyboard shortcuts (VS Code keys jdBasic supports)

| Key | Action |
| --- | --- |
| `F5` | Start debugging / Continue |
| `Shift+F5` | Stop debugging |
| `Ctrl+Shift+F5` | Restart debugging |
| `F9` | Toggle breakpoint on the current line |
| `F10` | Step Over |
| `F11` | Step In |
| `Shift+F11` | Step Out |
| `Ctrl+S` | Save (recompiles + repositions while debugging) |
| `F12` / `Ctrl+Click` | Go to Definition |
| `Ctrl+Shift+O` | Go to Symbol in file (FUNC/SUB/TYPE) |
| `Ctrl+Space` | Trigger autocomplete |
| `Ctrl+Shift+Space` | Trigger signature help |
| mouse hover | Variable value (debugging) or function docs (editing) |
| `Ctrl+Shift+M` | Problems panel (diagnostics) |
| `Ctrl+Shift+D` | Run and Debug view |
| `Ctrl+Shift+X` | Extensions view |

You can also right-click a `.jdb` file or use the editor title icons to
**Run File** (no debugging) or **Debug File**.

## 7. Command palette

`Ctrl+Shift+P`, then:

- **jdBasic: Lint Current File** forces a lint and opens the `jdBasic Lint`
  output channel (handy for troubleshooting the runtime path).

## 8. Troubleshooting

- **No squiggles / empty autocomplete / empty hovers**: `jdbasic.runtime`
  points at a build too old for `--lint` / `--dump-symbols` / `--dump-help`.
  Set it to a current `jdBasic.exe`. The linter shows a warning with an
  **Open Settings** action when it detects this.
- **Garbled debug output**: the debugger and runtime versions are mismatched.
  Use a matching `jdBasic.exe` and this extension version.

The full language reference is at
https://github.com/AtomiJD/jdBasic/blob/master/doc/languages.md

Happy coding!
