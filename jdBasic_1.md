# jdBasic

## A Persistent Experimental BASIC Environment

**jdBasic** is a modern cross-platform BASIC interpreter designed for **experimentation, scripting, and rapid development without friction**.

It combines the immediacy of classic BASIC with powerful built-in capabilities such as array programming, automation tools, graphics, sound, and a persistent REPL workspace.

Instead of constantly restarting environments, importing libraries, or rebuilding context, jdBasic lets you keep a computing session alive and evolving.

You can:

- explore ideas in a powerful interactive REPL
- save entire sessions with `SAVEWS`
- restore them instantly with `LOADWS`
- build automation tools and data pipelines
- prototype graphics, games, and experiments
- extend the language with native DLL modules

> **The goal of jdBasic is simple:**
>
> **Reduce friction between thinking and running code.**

---

## Try jdBasic Instantly

Run jdBasic directly in your browser:

https://jdbasic.org/live/index.html

No installation required.

---

## A 10-Second Example

Vector math works instantly — no imports or libraries.

```basic
V = [10, 20, 30, 40]

PRINT V * 2
PRINT V + 100
````

Output:

```basic
[20 40 60 80]
[110 120 130 140]
```

Arrays behave like built-in NumPy.

---

## The jdBasic REPL Workspaces

The jdBasic console is designed as a **persistent computing environment**, not just a temporary REPL.

Key capabilities include:

- **4 independent workspaces** (`F1–F4`)
- **Syntax highlighting**
- **Command history viewer** (`F7`)
- **History search** (`F8`)
- **Autocomplete for commands, custom types, maps, and COM objects**
- **Persistent command history**
- **SAVEWS / LOADWS** to store full sessions
- **RECUR / CLEAR_RECUR** timer tasks for live console dashboards

This allows workflows like:

- database exploration consoles
- automation toolkits
- log monitoring dashboards
- experimental scratchpads
- long-running investigations

Example:

```basic
SAVEWS "debug_session"

' tomorrow
LOADWS "debug_session"
```

Your variables, code, and history return instantly.

---

## Key Language Features

### Array Programming (APL-Inspired)

Arrays are first-class citizens.

- N-dimensional arrays
- automatic vectorization
- broadcasting operations
- functional pipelines

Example:

```basic
result = IOTA(10) |> FILTER(lambda x -> x > 5, ?) |> SELECT(lambda v -> v * 10, ?)

PRINT result
```

Output:

```basic
[60 70 80 90 100]
```

---

### Reactive Variables

Spreadsheet-style dependency propagation.

```basic
DIM A = 10
DIM B = 5

DIM C AS REACT INTEGER
C -> A * B

PRINT C
```

Change dependencies:

```basic
B = 20
PRINT C
```

`C` updates automatically.

---

### Immediate Mode GUI

jdBasic includes an **ImGui wrapper** for building tools quickly.

```basic
SCREEN 800, 600, "My Tool"

DO
    CLS

    IF GUI.BEGIN("Panel", 50, 50, 300, 200) THEN
        GUI.TEXT "Hello jdBasic"
        IF GUI.BUTTON("Click") THEN
            PRINT TIME$
        ENDIF
    ENDIF

    GUI.END()

    SCREENFLIP
LOOP
```

Perfect for:

- debugging tools
- visual utilities
- quick data dashboards

---

### Automation & System Integration

jdBasic works well as a **glue language**.

Capabilities include:

- filesystem utilities
- regex processing
- CSV and JSON handling
- HTTP APIs
- OS command execution
- Windows COM automation
- dynamic DLL imports

Example:

```basic
result$ = OS.EXEC("git status")
PRINT result$
```

---

### Live Coding Audio Engine

The built-in sequencer allows algorithmic music.

```basic
SOUND.SEQ track, pattern$, waveform$
SOUND.BPM 120
```

Supports:

- ADSR envelopes
- samples
- delay / reverb
- sidechain effects

---

## Cross-Platform

jdBasic runs on:

- Windows
- Linux
- macOS
- WebAssembly (browser)

The same language works in:

- terminal REPL
- GUI programs
- browser demos

---

## Extending jdBasic

The interpreter can load **native modules** via DLL / shared libraries.

This allows integration with external systems like:

- PyTorch
- TensorFlow
- SQLite
- Eigen
- custom native code

Example:

```basic
IMPORTDLL "mylib.dll"
```

---

## Typical Use Cases

jdBasic is particularly useful for:

### Rapid Experimentation

Try ideas instantly in a powerful REPL.

### Automation & Dev Tools

Build scripts, pipelines, and system tools.

### Learning Programming

Classic BASIC syntax with modern capabilities.

### Creative Coding

Graphics, games, and algorithmic music.

### AI Experimentation

Tensor operations and autodiff built into the language.

---

## Getting Started

Documentation:

Language reference
[https://github.com/AtomiJD/jdBasic/blob/master/doc/languages.md](https://github.com/AtomiJD/jdBasic/blob/master/doc/languages.md)

Manual
[https://github.com/AtomiJD/jdBasic/blob/master/doc/manual.md](https://github.com/AtomiJD/jdBasic/blob/master/doc/manual.md)

Online REPL
[https://jdbasic.org/live/index.html](https://jdbasic.org/live/index.html)

VS Code Extension
[https://github.com/AtomiJD/jdBasic/blob/master/vscode_extension/vscode_readme.md](https://github.com/AtomiJD/jdBasic/blob/master/vscode_extension/vscode_readme.md)

---

## Building from Source

Linux / macOS instructions:

```basic
doc/build_linux_macos.md
```

Windows users can open the Visual Studio solution.

---

## Contributing

Contributions, experiments, and feedback are welcome.

jdBasic is evolving rapidly and new ideas are encouraged.

---

## Philosophy

Modern development environments often require:

- dependency management
- library ecosystems
- virtual environments
- large frameworks

jdBasic explores a different direction:

**Build powerful capabilities directly into the interpreter.**

The result is a compact environment where experimentation feels immediate again.

```basic
Think → Type → Run
```

No setup required.

---

## What This Version Fixes

### Stronger first impression

Explains **what jdBasic is for**, not just what it contains.

### Clearer sections

GitHub readers skim — this structure supports that.

### Workspaces highlighted

Your **unique REPL feature** is now visible early.

### Better example flow

Quick examples appear before heavy feature explanations.

---
