# Release Notes

Convention: one section per released version, newest at the top. Pre-release / unreleased changes go under **Unreleased**.

---

## v1.0 Build 82 - 2026-08-31

Refresh of all four Windows bundles. Four pieces of syntax, one fix to a
builtin that answered the wrong question quietly, and the interpreter now
runs on microcontrollers.

### Highlights

- **Optional parameters with defaults**, for `FUNC` and `SUB`. Builtins have taken optional arguments all along - `SUM(a)` and `SUM(a, axis)` - and now so do the functions you write: `FUNC Greet(name$, greeting$ = "Hello", mark$ = ".")`. Defaults are literals, because a default that had to be evaluated would need a scope to be evaluated in and there is none where a function is declared. Optional parameters come last, since arguments are matched left to right. A wrong count names the range it wanted.
- **`??` gives the left side unless it is absent.** Absent, not falsy: `m{"port"} ?? 8080` keeps a port of `0`, where `ORELSE` would replace it. This matters because the hand-written test, `x = NONE`, is a documented trap - that comparison is true for values that are not NONE at all, and `TYPEOF(v) = "NONE"` is the correct long form.
- **`?.` `?{` `?[` read into something that may not be there.** A missing key already reads as NONE on its own; it is the *next* step that fails. The guard stops the chain instead: `reply?{"choices"}?[0]?{"message"}?{"content"} ?? "no answer"`. It is not a bounds check - an index out of range stays an error - and it guards a field, not a call.
- **`$"..."` puts expressions inside a string**: `$"Hallo {{name$}}, du hast {{n}} Nachrichten"`. Double braces, because single ones are what JSON is made of and angle brackets are what HTML is made of, and neither should need escaping. It becomes an ordinary concatenation at parse time, so it compiles exactly as well as it interprets.
- **`DIR$` of a directory lists that directory.** It used to split the name at its last slash, look for a file of that name in the parent, find nothing, and answer with an empty list - which reads as "the directory is empty" rather than "you meant something else". Fixed on Windows and POSIX alike, so both answer the same.
- **Native compiler**: a call with the wrong number of arguments is now a compile error naming the line and the range it wanted, instead of an LLVM IR verification failure naming a mangled function.
- **Faster**: builtins carry their own arity instead of wearing a wrapper (about 9% off the builtin call path), reductions walk their input instead of copying it, and `SLEEP` waits against a deadline rather than counting slices, so a long sleep no longer drifts.

### On a board

The same interpreter now runs on microcontrollers, under `embedded/`. The RP2350 build is what a PicoCalc is. The ESP32-S3 build runs on a bare DevKitC and on the 2.8 inch ES3C28P display board, where it boots into its own prompt on the panel: 320 by 240 graphics, a 40 by 30 text console, the editor, a capacitive touch screen, sound with the classic `PLAY` notation, a microphone, and an SD card at `/sd` beside the flash store. Measured on the board: a full frame is 32 ms, and a game redrawing only the band that moved is 7 ms.

None of this is in the Windows bundles - it is a separate build tree - but the language is the same one documented in `doc/languages.md`, without exception.

### Documentation

`help.txt` gained twenty-six entries covering the board verbs, six of which were older debt from the PicoCalc. `doc/languages.md` gained a chapter for the ports. The VS Code extension is at 1.0.30: its grammar generator only ever scanned `src/`, so the 108 builtins the board ports register elsewhere had never been coloured. `doc/BUILD.md` and the gate skill both listed the files that require a runtime-DLL rebuild and both omitted `src/vm.cpp`, which is the one that catches people - the symptom is an interpreter that is right and every generated EXE stale.

### Distribution

The same four Windows x64 bundles, all Authenticode-signed: **core**, **mcp-native**, **vibe-game-pack**, **vb6**. SHA256 hashes are on the [release page](https://github.com/AtomiJD/jdBasic/releases).

---


## v1.0 Build 78 - 2026-08-14

Refresh of all four Windows bundles. No new bundle, no new build flag. Supersedes Build 77 from the same day, which shipped the identical binaries with a `help.txt` that still predated the Forms work.

### Highlights

- **Forms grew the rest of the property surface**: `FORECOLOR` / `BACKCOLOR` as `0xRRGGBB`, `FONT` as a map of `name`/`size`/`bold`/`italic`/`underline`/`strike`, plus `TAG`, `TOOLTIP`, `ALIGN`, `MAXLENGTH`, `PASSWORD`, `LOCKED`, `TABSTOP`, `TABINDEX` and `CURSOR`. A form paints its own background with `BACKCOLOR`; a themed push button ignores colours, the same rule VB6 had.
- **The forms event vocabulary**: every control that takes input now fires `GOTFOCUS`, `LOSTFOCUS`, `KEYDOWN`, `KEYUP`, `KEYPRESS`, `MOUSEDOWN`, `MOUSEUP` and `MOUSEMOVE` on top of its own events, all binding by name without an `ON`. Keyboard events carry `key`, mouse events `button` and `x`/`y`, both carry `shift`, `ctrl` and `alt`.
- **A form can refuse to close**: `NAME_UNLOAD` runs synchronously while the window still exists, so a handler can read its own controls one last time and cancel the close with `e[0]{"cancel"} = TRUE` - the VB6 `Form_QueryUnload` pattern, for the window cross, `Alt+F4` and `FORM.CLOSE` alike.
- **`FORM.POPUP(frm, spec, [x], [y])`**: context menus with the same spec shape as `FORM.MENU`, dispatching `NAME_CLICK` like a menu-bar item.
- **`.jdform` carries the appearance properties**, so what the VS Code visual designer paints is what the program shows.
- **Array fixes** in every bundle: `ROTATE` and `SHIFT` move every axis instead of only the outer one, and a gather keeps the shape of its index set. Note the deliberate sign difference: a positive `ROTATE` pulls from ahead (`out[i] = in[i + k]`), a positive `SHIFT` pushes along (`out[i] = in[i - k]`).
- **Native compiler**: a parameter called with two different types is refused at compile time instead of being silently mis-compiled.

### Documentation

`help.txt` caught up with the Forms work and now has an entry for every symbol the runtime reports except the five compiler internals: the full `FORM.SET` / `FORM.GET` property lists, `FORM.POPUP`, the event vocabulary, and the corrected `ROTATE` / `SHIFT` semantics. `doc/languages.md` gained the previously undocumented `GFX.*` batch and image calls, `GUI.PUSH_STYLE_COLOR` / `GUI.POP_STYLE_COLOR`, `HTTP.SERVER.WAIT`, `SOUND.NOTE` / `SOUND.STATS`, `SCREENWIDTH` / `SCREENHEIGHT`, `FUNCS`, the `JDB.*` state helpers, the sized numeric types and the VB6 aliases.

### Distribution

The same four Windows x64 bundles, all Authenticode-signed: **core**, **mcp-native**, **vibe-game-pack**, **vb6**. SHA256 hashes are on the [release page](https://github.com/AtomiJD/jdBasic/releases).

---

## v1.0 Build 76 - 2026-07-30

Refresh of all Windows bundles (builds 74/75 were internal) plus a brand-new fourth bundle.

### Highlights

- **Native Win32 forms** (`FORMS` build flag): a full retained-mode toolbox of real Win32 controls - buttons, lists, tree/list views, tabs, sliders, date pickers, menus with accelerators, toolbars, status bars, MDI child windows and the common dialogs. Events bind by naming convention (`SUB <control>_<event>`), layouts can live in declarative `.jdform` JSON files (`FORM.LOAD`), and the same source compiles to a standalone `.exe` with `jdbasic -c`. The VS Code extension gained a **visual form designer**.
- **New VB6 Pack bundle** (`jdbasic-vb6-windows-x64`): COM automation + Forms + embedded SQLite + GFX/ImGui/HTTP + MCP in one download, with the forms demos and `.jdform` layouts included.
- **Audio FX subsystem** (`FX` build flag): a WAV/effect-chain engine with a JSON-driven ImGui FX rack, live oscilloscope and FFT spectrum, chromatic tuner (`MON.PITCH`), tap tempo, multi-CC **MIDI** mapping (`MIDI` flag, RtMidi), record-while-monitoring, and `FX.SET` / `FX.DUMP$` for live chain tuning from the REPL.
- **Web stack**: the JDWEB mini framework (sessions, cookie login, themed pages) on the `TMPL` template engine, `HTTP.SERVER.WAIT` for long-running handlers, `HTTP.SERVER.ON_NOTFOUND`, and the jdTrakr kanban board as a deployable reference app - see `doc/WebDev.md`.
- **Embedded SQLite** (`SQLITE` build flag): `SQL.OPEN/EXEC/QUERY/...` statically linked, no external DLL.
- `PDF.TEXT$` builtin for local PDF text extraction.
- All GFX bundles now ship the default font (`jdbasic_default.ttf`) next to the EXE, so `TEXT` works out of the box on a clean machine.

### Distribution

Four Windows x64 bundles, all Authenticode-signed: **core**, **mcp-native**, **vibe-game-pack**, and the new **vb6** pack. SHA256 hashes are on the [release page](https://github.com/AtomiJD/jdBasic/releases).

---

## v1.0 Build 73 - 2026-06-22

First public 1.0 release of the v2 bytecode-VM rewrite.

### Highlights

- Bytecode compiler + virtual machine with APL-style array vectorization, Eigen-backed linear algebra (`SVD`/`QR`/`DET`/`EIG`) and `FFT`/`IFFT`.
- Native compilation to standalone Windows EXEs (`jdbasic -c`) via an embedded LLVM backend.
- SDL3 graphics + Dear ImGui, a sprite / tilemap / camera / particle game layer, and SDL_mixer audio.
- **MCP server** (`jdbasic --mcp` stdio, `--mcp-http 7321` HTTP) exposing the persistent VM to any MCP-aware client (Claude Code/Desktop, Cursor, Cline, Continue, Zed, Windsurf). Live `jdb_stop` / `jdb_resume` / `jdb_recompile` enable AI pair-coding on a running program. See [`doc/MCP.md`](doc/MCP.md).
- Local AI stack (feature-flag builds): llama.cpp LLMs, ONNX models, dense embeddings, RAG and a k-NN text classifier.
- In-REPL `HELP` and editor hover/signature data driven by the bundled `help.txt` (full builtin reference) and `--dump-symbols` / `--dump-help`.

### Distribution

Three Windows x64 bundles:

- **core** (`jdbasic-core-windows-x64`) - interpreter + MCP server, no LLVM (~4 MB).
- **mcp-native** (`jdbasic-mcp-native-windows-x64`) - adds the LLVM native-compile toolchain (~33 MB).
- **vibe-game-pack** (`jdbasic-vibe-game-pack-windows-x64`) - GFX/ImGui build with ready-to-run game demos (~7 MB).

All bundles ship the app-local Visual C++ runtime (so they run on a clean Windows with no redistributable installed), the full `help.txt` reference, `doc/languages.md`, and `THIRD_PARTY_LICENSES.txt`.

---

## v1.0 Build 2 - 2026-04-15

(Existing release; populate retroactively from `git log` if desired.)

---

## Template for new entries

```
## vX.Y Build N - YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Breaking
- ...

### Distribution
- ...
```
