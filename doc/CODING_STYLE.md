# Coding Style

Verbindliche Konventionen für `src/` (C++) und `fluppi/`, `tests/`, `jdb/` (jdBasic).
Ziel: **SPOT** - Single Point of Truth. Der Code erklärt WAS er tut; Kommentare nur wenn das WARUM nicht offensichtlich ist.

## C++ (src/)

### Formatting
- 4-Space-Indent, keine Tabs
- LF Line-Endings (`.gitattributes` erzwingt das)
- Curly-brace same line: `if (x) {`
- Max. Zeilenlänge ~100 Zeichen - weiche Grenze, Lesbarkeit schlägt Hard-Limit

### Naming
- `snake_case` für Funktionen, Variablen, Felder, Methoden
- `PascalCase` für Typen: `struct`, `class`, `enum class`
- `ALL_CAPS` nur für Macros und Compile-Time-Konstanten (`JD_TAG_*`)
- Private Felder kein `m_`-Prefix, ausser für transient codegen-state (`m_want_leaf_tag`)
- Header `pragma once`, kein Include-Guard

### Types
- `enum class` > plain `enum`. Plain enum nur wenn C-interop nötig (→ dann parallel `#define`-Aliase wie `JD_TAG_*`)
- Keine magic numbers für getaggte Werte - immer die named constant aus dem zentralen Header
- `const` wo es geht: Parameter, Locals, Methoden
- Brace-Init bei Struct-Literalen: `{ val, JD_TAG_F64 }` statt `TypedValue(val, 1)`

### Comments - die SPOT-Regel
**Default ist KEIN Kommentar.** Schreibe nur einen wenn:
1. Es gibt eine versteckte Invariante, die der Code nicht direkt zeigt
2. Ein Workaround für einen spezifischen Bug (kurz: was & warum)
3. Eine überraschende Konstante oder nicht-offensichtliche Korrektheit
4. Public-API in Headern - ein Einzeiler zu Semantik und Ownership

**Nicht schreiben:**
- Was-der-Code-tut Narrative (`// Convert index to i64` direkt vor `LLVMBuildFPToSI`)
- Historien-Kommentare (`// used to be X, now Y`) - das steht im git log
- Phase-Marker (`// Phase 3b:`) - Phasen-Historie gehört in Commit-Messages
- Section-Banner (`// ══ PLAYER STATS ══`) - wenn die Datei strukturierte Sections braucht, splitte sie

**Wenn du einen Kommentar schreibst:**
- Eine Zeile bevorzugt, Block nur wenn echt nötig
- Fokus auf WARUM, nicht WAS
- Wenn der Kommentar ein Datum / Ticket / „see issue #123" enthält - lass ihn, das ist Kontext der nicht im Code steht

### Error Handling
- `report_error(file, line, msg)` sammelt Diagnostics - alle nach dem Pass, nicht first-fail
- Keine Silent-Fails: wenn eine Dispatch-Entscheidung unerwartet ist, throw oder diagnostics

### Tag System
- Alle Tag-Literale via `JD_TAG_*` aus `jdb_tags.h`
- `VarInfo.tag`, `TypedValue.tag`, `return_tag`, `param_tags[i]` → immer Macro
- Der Kommentar `// JdTag (see jdb_tags.h)` auf dem Feld ist ausreichend

## jdBasic (fluppi/, tests/, jdb/)

### Compiler-Targets
- `OPTION "EXPLICIT"` und `OPTION "STRICT"` verpflichtend am Dateianfang
- Jede Variable deklariert via `DIM` oder initialisiert via `LET`
- `AS <Type>` bei allen DIMs wo der Typ nicht aus dem Init-Expr eindeutig ist

### Interpreter-only Scripts
- Dürfen bare-assignments nutzen (loose mode)
- Wenn ein Skript sowohl im Interpreter läuft als auch ggf. compiliert wird: behandle es als Compiler-Target

### Naming
- `snake_case` für alles (jdBasic ist case-insensitive, aber konsistente Schreibweise erleichtert `grep`)
- `$`-Suffix für String-Variablen
- Module-Namen als `SUB_AREA.NAME` (z.B. `RPG_DIALOG.SAY`)

### Struktur
- Imports oben nach `EXPORT MODULE`
- Public-API via `EXPORT` markiert
- Keine Whole-File Comment-Banner ausser am obersten Dateiheader (`' == Module Title ==` Block)

## Commits
- Ein Stable-Milestone → ein Commit
- Betreff in Imperativ, unter 72 Zeichen
- Body erklärt WARUM (was sieht man ja im Diff), und welche Tests gelaufen sind
- Kein `Co-Authored-By` Trailer
- Referenz zur Phase oder Ticket wenn relevant

## Tests
- Jede neue Sprach-Feature → Regression-Test VOR dem Fix schreiben
- Neue Keywords → eintragen in `doc/languages.md`, `doc/help.txt`, `jdbasic.tmLanguage.json`
- 5-Point-Matrix nach jeder Compiler-Änderung: comprehensive (interp+native), crash (interp+native), rpg_demo native
