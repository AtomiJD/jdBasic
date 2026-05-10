# FTXUI-Refactor: REPL + Editor

**Datum:** 2026-05-10 · **Owner:** Atomi · **Helper:** 2-5h/Woche

## Ziel

REPL (`console.cpp`, 1348 LoC) und Editor (`editor.cpp`, 1540 LoC) — beide handgeschnitzte raw-mode-Implementierungen mit eigenem ANSI-Render — auf [FTXUI](https://github.com/ArthurSonzogni/FTXUI) umstellen. Saubere Komponenten, modernes Look-and-Feel, cross-platform aus der Box (Windows 10+/PowerShell 7+/Linux/macOS), weniger Code zum Pflegen.

## Warum FTXUI (kurz)

| Pro | Contra |
|---|---|
| Component-model (Container, Renderer, Input, Menu, Tabs) | +1 dependency (~5MB sources) |
| Declarative layout (hbox/vbox/gridbox/border/separator) | Build-Integration für `build.bat` nötig |
| 256-/RGB-Color out of the box | Konsole muss ANSI-fähig sein (alle modernen Terminals OK) |
| Mouse + Resize-Events | Nicht so direkt-kontrolliert wie raw mode |
| Input-Komponente eigener mit cursor + scroll + selection | Lernkurve für functional component style |
| Cross-platform (Win/Lin/Mac) ohne ifdefs in unserem Code | Owns the screen — coexistence-Phase braucht `--ftxui` Flag |

**Alternativen kurz erwogen, abgelehnt:**
- **ImTUI** — wäre symmetrisch zu unserem ImGui-`gui.cpp`, aber weniger gepflegt, schwächeres Layout-System für Editor-Use-Case
- **ncurses** — POSIX-only, aufwendige Win32-Layer, hässliches Look
- **Notcurses** — mächtiger aber C-API + komplexer
- **Status quo + incremental polish** — keine Reduktion der Wartungslast, kein "neuer Look"

## Architektur-Entscheidungen vorab

1. **FTXUI vendored in `libs/ftxui/`** — kein git submodule (Atomi will Repo self-contained), Update via `git pull` im libs-Tree
2. **Static-Link** — eine Lib mehr im Build, keine DLL-Streuung
3. **Coexistence via `--ftxui` Flag** während Migration — alter Pfad bleibt working, neuer baut sich daneben
4. **Per-Plattform-Build:**
   - Windows: build.bat erweitert mit `FTXUI` Flag (compiles `libs/ftxui/src/**`)
   - Linux: build.sh analog
5. **Color-Palette** matching der jdBasic.org Site (teal/dark-blue main scheme) — eine zentrale `ftxui_theme.h` für Konsistenz
6. **Streaming-Output** vom VM (`vm.on_output` aus PRINT) muss in eine FTXUI-Component fließen — Lösung: thread-safe ring-buffer + `screen.PostEvent()` zum Refresh

## Phasen-Plan

### Phase 0 — Build-Integration + Hello-World (~4-6h)

- FTXUI sources in `libs/ftxui/` ablegen (Git-Tag-checkout, ~140 .cpp/.hpp Dateien)
- `build.bat` Flag `FTXUI` hinzufügen: kompiliert `libs/ftxui/src/**` zu Static-Lib `ftxui-component.lib`, `ftxui-dom.lib`, `ftxui-screen.lib`
- `build.sh` analog für Linux
- Mini-Programm `tests/ftxui_hello.cpp` — zeigt ein Border + "Hello jdBasic" in den Theme-Farben — als Compile-Smoke
- Build erfolgreich auf Windows + Linux (Strix Halo + NVIDIA box)

**Risiko:** FTXUI nutzt CMake. Wir ohne CMake. → entweder die ~140 .cpp Dateien manuell in build.bat enumerieren (eine Zeile auto-glob in PowerShell), oder einen kleinen CMake-Wrapper für nur die FTXUI-Lib + dann von build.bat den Lib-File ins Linker-Kommando.

### Phase 1 — REPL-Prototype (parallel zu console.cpp) (~6-8h)

- Neue Datei `src/repl_ftxui.cpp` mit minimaler REPL:
  - Eine Input-Komponente (jdBasic-Source-Zeile)
  - Eine Output-Box (ScrollableContainer mit den letzten N PRINT-Ausgaben)
  - Enter → execute, Output-Box wächst
- Wire-Up: `vm.on_output` schreibt in einen `std::deque<std::string>` mit mutex; jede Mutation `screen.PostEvent(Event::Custom)` → Refresh
- Main.cpp: `--ftxui` Flag schaltet zwischen alter `Console::run()` und `repl_ftxui_run(vm)` um

**Demo-Zustand:** `jdBasic --ftxui` startet eine TUI mit 1 Workspace, kein History, kein Syntax-Color. Nur "Hello World" + Eingabe + Output. Sichtbarer Beweis dass die Plumbing funktioniert.

### Phase 2 — REPL Feature-Parität + Polish (~8-10h)

- **History** (Up/Down) — eigene `std::deque<std::string>`, lokale Komponente
- **Workspace-Tabs** statt F1-F4 als Hotkey: oben sichtbare Tabs (`[1] Main` `[2] Sandbox` `[3] Tests` `[4] Notes`), klickbar UND F1-F4
- **Syntax-Color im Code-Echo** — gleicher Tokenizer wie editor.cpp, dann FTXUI `text() | color(...)` chain
- **Multi-line Paste-Detection** — wenn Input mehrere Zeilen, automatisch Buffer-Mode bis Leer-Zeile
- **Command-Palette** Ctrl+P — fuzzy filter über Commands (`load`, `save`, `clear`, `vars`, `funcs`, …)
- **Status-Bar** unten: Build-Nummer, current Workspace, Vars-Count, Time
- **Side-Panel toggle** (Ctrl+B) — zeigt aktuelle User-Globals + FUNCs (read-only, refresh on every prompt)
- **Theme application** — Palette-Variable, eine Stelle ändern wechselt Look

**Wichtig:** alter `console.cpp` bleibt funktionsfähig; default ist alt, `--ftxui` ist neu opt-in.

### Phase 3 — Editor-Prototype (parallel zu editor.cpp) (~6-8h)

- Neue Datei `src/editor_ftxui.cpp`
- FTXUI-Eingabe: ein selbst-gebautes "Buffer"-Component (FTXUI's stock Input ist 1-line; Editor braucht eine N-line-Variante mit cursor x/y + viewport)
- Render: Zeilen-Nummern links, Code-Buffer Mitte, Status-Zeile unten
- Tastatur: Pfeiltasten, Home/End, PgUp/PgDn, Backspace/Delete/Enter, Tab
- Save (Ctrl+S) + Quit (Ctrl+Q) als minimal viable

### Phase 4 — Editor Feature-Parität (~10-15h)

- **Syntax-Highlight** — gleiche Tokenizer wie REPL, gleiches Theme
- **Undo/Redo** (Ctrl+Z/Y) — Stack of buffer-snapshots (oder operational transform für Speicher-Effizienz)
- **Find/Find-Next** (Ctrl+F / F3) — modal Input für Suchterm, highlight Treffer, n/N navigiert
- **Goto-Line** (Ctrl+G) — modal Input
- **Copy/Cut/Paste** (Ctrl+C/X/V) — line-based wie aktuell, plus selection (Shift+Pfeil) für char-range
- **Selection-Render** — invertierte Hintergrundfarbe per Char-Range
- **F5 = Save + Compile + Run** wie aktuell — handoff an existing Compile-Pipeline
- **System-Clipboard-Integration** — Win: WinAPI; Lin: xclip/wl-copy mit Fallback auf internen Buffer
- **Mouse-Support** — Click positioniert Cursor, Drag selektiert
- **Bracket-Matching** — Highlight matching `(` / `)` / `THEN`-`ENDIF` etc.

### Phase 5 — Polish + Querschnitt (~4-6h)

- **Konsistente Theme-Datei** `src/ftxui_theme.h`: Color::Teal3, Color::DarkSlateGray, etc. mit Names
- **Build-Number-Banner** beim REPL-Start (so wie aktuell)
- **`/help` Command-Palette** — listet alle Slash-Commands mit Beschreibung
- **F12 = Toggle DevTools** — ein Side-Panel mit current scope vars, last error stack, recent func calls (wenn man's mag)
- **Smooth-Resize** — Layout adjusts on terminal-resize ohne flicker

### Phase 6 — Cleanup (~2-4h)

- `--ftxui` wird default
- Alter `console.cpp` + `editor.cpp` per `--legacy-tui` weiterhin verfügbar (sicherheits-Net), default off
- Doku-Update auf jdBasic.org (Tutorial-Screenshots)
- Build-Flag `FTXUI` wird mandatory in build.bat default-line

### Phase 7 — Linux-Validierung (~4-6h)

- Strix-Halo build + smoke-test des FTXUI-REPL + Editor
- NVIDIA-Box dito
- Falls Probleme: tmux/screen/ssh-tunnel test, true-color vs 256-color fallback prüfen
- Headless-CI test (xvfb? — nein, FTXUI ist terminal-only, normale CI sollte gehen)

## Aufwands-Schätzung

| Phase | Aufwand |
|---|---|
| 0  Build-Integration | 4-6h |
| 1  REPL-Prototype | 6-8h |
| 2  REPL-Parität + Polish | 8-10h |
| 3  Editor-Prototype | 6-8h |
| 4  Editor-Parität | 10-15h |
| 5  Polish | 4-6h |
| 6  Cleanup | 2-4h |
| 7  Linux-Validierung | 4-6h |
| **Total** | **44-63h** |

Bei Atomi 30-40h/Woche + Helper 2-5h/Woche → **realistisch 2 Wochen**, besser 3 für Polish-Spielraum.

## Was Helper übernehmen kann

- Theme-Datei aufsetzen + jdBasic.org Farben rüber-pickeln
- Hello-World-Test-Script
- Manuelle Test-Pässe nach jeder Phase: "drück alle Tasten, melde was bricht"
- Doku-Screenshots für die Site
- xclip/wl-copy Linux-Test

## Risk-Register

| Risiko | Wahrscheinlichkeit | Mitigation |
|---|---|---|
| Build-Integration aufwendiger als gedacht | Mittel | Phase 0 mit klarem Time-Box (1 Tag), wenn nicht durch → CMake-Wrapper-Lösung |
| FTXUI Input-Komponente zu limitiert für Editor | Niedrig | Editor-Buffer custom; FTXUI für Layout/Render only |
| ANSI-Escape-Konflikte mit jdBasic-PRINT-Strings | Niedrig | Alle PRINTs gehen ins Buffer als Plain-String; FTXUI rendert |
| Cross-Platform-Bug auf Strix Halo | Niedrig | FTXUI well-tested auf Linux; falls Konsole zu alt → fallback |
| Migration bricht Mid-Stream | Mittel | Coexistence-Strategy: alter Pfad ALWAYS funktioniert, neu opt-in via Flag bis Phase 6 |
| Helper-Bandwidth fällt aus | Niedrig | Plan ohne Helper-Critical-Path; Helper liefert nice-to-haves |

## Erfolgs-Metriken

- ✅ `--ftxui` REPL-Modus startet auf Win + Linux ohne Fehler
- ✅ Alle existing-REPL-Funktionen verfügbar (history, workspaces, vars, funcs, load, save, run, etc.)
- ✅ Editor F5 → Compile + Run funktioniert end-to-end
- ✅ Visuell deutlich moderner als alter Pfad (vor-nachher Screenshot)
- ✅ LoC-Reduktion: alter console.cpp (1348) + editor.cpp (1540) = 2888 → neuer FTXUI-Pfad geschätzt 1500-1800 LoC (35% weniger)
- ✅ Pre-commit Gate grün (alle bestehenden Tests laufen weiter)

## Decision-Points / Offen

1. **Wann `--ftxui` Default wird** — nach Phase 6 oder erst nach 1-2 Wochen Stabilitäts-Soak?
2. **Workspace-Modell** — bleiben es 4 fixe Workspaces oder dynamisch erstellbar?
3. **MCP-Live-Tweak im FTXUI-REPL** — soll die TUI Live-Updates anzeigen wenn MCP gerade was eval'd? (nice-to-have, Phase 5+)
4. **Plugin-System für Editor-Modes** — XML/JSON/CSV-Mode neben jdBasic? Vorerst nein.

## Pickup-Reihenfolge

Ich würde sagen: **erst Phase 0+1 als ein Sprint** — wenn nach 1-2 Tagen die FTXUI-Build-Pipeline solide steht und ein "echo"-REPL läuft, ist der Rest ein Geradeauslauf. Wenn Phase 0 sich aber als Build-Hell entpuppt, lieber early abort und mit ImTUI oder Status-quo-polish weitermachen.

Vorschlag konkret: morgen Phase 0 starten, parallel zum Strix-Halo-Catchup. Helper kann das Theme-File schon mal vor-vorbereiten.
