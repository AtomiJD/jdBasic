# jdBasic - A Persistent Experimental BASIC Environment

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

The goal of jdBasic is simple:

> **Reduce friction between thinking and running code.**

## Try jdBasic in your browser

You can try the language immediately:

[jdbasic.org/live](https://jdbasic.org/live/index.html)

(No installation required.)

## 🚀 Key Features

### 🖥️ Immediate Mode GUI (ImGui)

Create fully functional graphical user interfaces with just a few lines of code. No complex callbacks or event listeners—just logic.

* Windows, inputs, sliders, plots, and tables.
* Integrated **Live Coding Sequencer** and **Sprite Engine**.

### ⚡ Reactive Programming

Bring the power of spreadsheet-style reactivity to your code.

* **Reactive Variables**: Define variables that automatically update when their dependencies change (e.g., `A -> B * 2`).
* **Graph Propagation**: The interpreter handles the dependency graph for you.

### 🔢 APL-Inspired Array Processing

* **N-Dimensional Arrays**: First-class support for vectors, matrices, and tensors.
* **Vectorized Math**: Operations apply to entire arrays automatically (e.g., `MyArray * 2`). Most built-in functions (`SIN`, `SQR`, `RIGHT$`, etc.) are vectorized and apply to every element automatically.
* **Data Analysis & Reduction**: Instantly aggregate arrays with `SUM`, `PRODUCT`, `MIN`, `MAX`, and perform cumulative operations with `SCAN`.
* **Data Pipelines**: Chain functions using the Pipe Operator (`|>`) and Lambdas for elegant data transformation.

  ![One liner Biorythm](https://github.com/AtomiJD/jdBasic/blob/master/resources/BioRythmOneLine.png)

### 🔌 Hardware & System I/O

* **Serial Communication**: Talk to Arduinos and embedded devices with `SERIAL` commands.
* **Joystick/Gamepad**: Native support for game controllers via `JOY`.
* **System Integration**: Clipboard access, extended Filesystem/Path utilities, and OS execution.

### 🌐 Cross-Platform & Web Ready

* Runs natively on **Windows**, **Linux**, and **macOS**.
* Runs in the **Web Browser** via WebAssembly (Emscripten).

---

## Language Tour

### 1. The Reactive Paradigm

Forget manually updating variables. With `REACT`, state management becomes automatic.

```basic
DIM BaseValue AS INTEGER = 10
DIM Multiplier AS INTEGER = 5

' Define Result as a reactive variable dependent on BaseValue and Multiplier
DIM Result AS REACT INTEGER
Result -> BaseValue * Multiplier

PRINT Result ' Outputs: 50

' Update the dependency
Multiplier = 10

' Result updates automatically without reassignment
PRINT Result ' Outputs: 100

```

### 2. Immediate Mode GUI

Creating tools is incredibly fast using the built-in ImGui wrapper.

```basic
SCREEN 800, 600, "My Tool"

DIM BgColor[4] = [0.2, 0.3, 0.3, 1.0]

DO
    CLS
    
    ' Start a window
    IF GUI.BEGIN("Control Panel", 50, 50, 300, 200) THEN
        GUI.TEXT "Welcome to jdBasic GUI"
        GUI.SEPARATOR()
        
        ' A button that does something immediately
        IF GUI.BUTTON("Click Me") THEN
            PRINT "Button was clicked at " + TIME$
        ENDIF
        
        ' Direct variable binding for color picker
        GUI.COLOR("Background", BgColor)
    ENDIF
    GUI.END()
    
    SCREENFLIP
    SLEEP 16
LOOP UNTIL INKEY$() = "q"

```

### 3. Functional Pipelines

Combine high-order functions with the pipe operator for clean data processing.

```basic
' Generate 10 numbers, filter > 5, multiply by 10, and format
result = IOTA(10) |> FILTER(lambda x -> x > 5, ?) |> SELECT(lambda v -> v * 10, ?)

PRINT result 
' Output: [60 70 80 90 100]

```

---

## 📦 What's New?

jdBasic is evolving rapidly. Here are the latest additions:

* **GUI & Graphics**: Full **ImGui** integration for building complex tools and debuggers.
* **Platform Support**: Official support for **macOS** and **Web Browsers** (WASM).
* **Reactive Engine**: New `REACT` keyword and `->` operator for automatic state propagation.
* **Hardware Control**:
* `SERIAL`: COM port communication for IoT/Embedded projects.
* `JOY`: Full Joystick and Gamepad input support.

* **Expanded Standard Library**:
* **CODEC**: Base64, SHA256, and UUID generation.
* **IO**: `DIR$()` (extended file lists), `PATH.*` helpers, and `CLIPBOARD` access.
* **Audio**: A built-in **Live Coding Sequencer** (`SOUND.SEQ`) for algorithmic music.

---

## Getting Started

* **Language Reference**: [View Documentation](https://github.com/AtomiJD/jdBasic/blob/master/doc/languages.md)
* **Manual**: [Read Manual](https://github.com/AtomiJD/jdBasic/blob/master/doc/manual.md)
* **Online REPL**: [Try it Online](https://jdbasic.org/live/index.html)
* **VS Code Extension**: [Get the Plugin](https://github.com/AtomiJD/jdBasic/blob/master/vscode_extension/vscode_readme.md)

### Building from Source

See [build_linux_macos.md](https://github.com/AtomiJD/jdBasic/blob/master/doc/build_linux_macos.md) for instructions on compiling for Linux and macOS. Windows users can open the solution in Visual Studio 2022.

Contributions and feedback are welcome!
