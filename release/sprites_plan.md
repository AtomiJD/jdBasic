# Sprites Refactor + AI-Sprite-Builtins — Plan
**Owner:** Atomi · **Erstellt:** 2026-05-21 · **Start:** **Do 2026-05-21 (vorgezogen)** · **Deadline:** vor Launch Do 2026-06-04 · **Live-Demo-Recording:** post-launch

## Ziel

Zwei Phasen + ein optionales Demo-Material:
1. **Refactor** — Sprite-Code aus `src/graphics.cpp` rausziehen in eigenes `src/sprites.cpp`, analog zur `src/tiledmap.cpp`-Trennung. Kein Verhaltensbruch, full Pre-Commit-Gate grün.
2. **AI-Sprite-Builtins** — neue Built-ins damit Claude (oder ein User-Script) Sprites zur Laufzeit aus RGB-Daten bauen, speichern und als Animation-Sequenzen verwalten kann.
3. **(Optional, später)** Live-Coding-Session aufgezeichnet — zweites Game ("Sprite-Quest" o.ä.) komplett mit den neuen Builtins gebaut, als YouTube-Content + Beweis-Material für die MCP-Story.

**Stellar Drift bleibt unverändert** — die LINE-Vektor-Optik ist Teil der Identität, kein Skin-Toggle nötig.

## Aktueller Stand (Ist-Analyse 2026-05-21)

- `src/graphics.cpp` = **3076 Zeilen**, 107 Sprite-Bezüge, enthält:
  - `struct Sprite { int cols; ... }`
  - `static std::map<int, Sprite> g_sprites;`
  - `static int g_next_sprite_id = 1;`
  - Helpers: `get_sprite()`, `draw_one_sprite()`, Spritesheet-Slicing-Logik
  - SPRITE.LOAD-Pfad bei Zeile ~1537 (`g_next_sprite_id++`)
- `src/tiledmap.cpp` = **995 Zeilen**, das saubere Vorbild für die Aufteilung
- SPRITE.*-Names sind in **drei** Dateien als String registriert:
  - `src/graphics.cpp` (Implementierung)
  - `src/vm.cpp` (Dispatch im Interpreter)
  - `src/llvm_codegen.cpp` (Native-Compile-Pfad)
  Der Refactor muss alle drei Touchpoints sauber halten.

---

## Phase 1 — Refactor (~3-4h)

**Deliverable:** Neue Datei `src/sprites.cpp` + Header `src/sprites.h`, `graphics.cpp` schrumpft um ~400-600 Zeilen, full Pre-Commit-Gate grün, kein User-sichtbares Verhalten verändert.

### Schritte

1. **Header-Schnittstelle definieren** — `src/sprites.h` mit:
   - `struct Sprite { ... }` (aus graphics.cpp rausgezogen)
   - `void register_sprite_builtins(VM& vm)` — analog zu `register_tilemap_builtins`
   - Forward-Declarations für die wenigen Hooks die `graphics.cpp` weiter braucht (z.B. wenn `RECT`/`LINE` mal auf Sprite-Daten lesen müsste — vermutlich keine, aber check)

2. **Code-Move**:
   - `g_sprites`, `g_next_sprite_id`, `get_sprite()`, `draw_one_sprite()` → `sprites.cpp`
   - Alle `if (name == "SPRITE.LOAD") { ... }` und Geschwister-Cases → in `register_sprite_builtins`
   - SDL_Texture-Handling bleibt im Sprite-Code (Lifecycle-Owner)

3. **vm.cpp + llvm_codegen.cpp synchron halten**:
   - `vm.cpp`-Dispatch ruft `register_sprite_builtins(*this)` einmalig auf
   - `llvm_codegen.cpp` — die Native-Compile-Strings für SPRITE.* einmal durchgehen, sicherstellen dass die `no_vectorize`-Listen weiter konsistent sind (siehe Memory `project_matrix_gfx_novec.md` — RECT/LINE pattern; SPRITE.DRAW etc. müssen analog beide Listen kennen)

4. **Build-Config**:
   - `build.bat` + `build_rt.bat` müssen `sprites.cpp` mit-kompilieren, hinter dem gleichen `GFX`-Flag wie graphics.cpp
   - Object-File `build/sprites.obj` in beide Link-Steps (EXE + DLL)

5. **Pre-Commit-Gate** (per [jdbgate-skill](../.claude/skills/jdbgate/SKILL.md)):
   - 4 Suites × Interp+Native + RPG-Demo + Emu-Run-Smoke
   - Speziell auf RPG/Stellar-Drift achten — beide nutzen Sprites
   - Wenn ein Test rot wird: zurück zu Schritt 2/3, was wurde verschluckt

### Risiken

| Risiko | Mitigation |
|---|---|
| SDL_Texture-Lifetime falsch übertragen, Memory-Leak | Vergleichs-Test: RPG-Demo 60s laufen lassen, Task-Manager-Speicher beobachten |
| Native-Compile-Liste vergessen → SPRITE.DRAW in -c-Modus crasht | beide `no_vectorize`-Listen in einem Edit anpacken, mit `-c` testen |
| graphics.cpp behält irgendwo eine `extern Sprite&`-Referenz | clean rebuild forcen, Link-Errors ernst nehmen statt schnell zu fixen |

---

## Phase 2 — AI-Sprite-Builtins (~4-5h on top)

**Deliverable:** Vier neue Builtins in `sprites.cpp`, ein API-Test-Skript in `tests/`, Doku-Update in `doc/languages.md` + `help.txt` + `jdbasic.tmLanguage.json` (siehe Memory `feedback_update_docs_on_new_commands.md`).

### Neue Builtins

1. **`SPRITE.CREATE(name$, w, h)`** — leere Textur im Registry, transparent (alpha=0) initialisiert. Return: Sprite-ID (Integer).
2. **`SPRITE.SETPIXEL(name$, x, y, r, g, b, a)`** — einzelner Pixel. Bequem fürs Skript, langsam bei großen Sprites.
3. **`SPRITE.SETBUFFER(name$, rgba_array)`** — Bulk-Setter. `rgba_array` ist ein flat-Array `[r,g,b,a, r,g,b,a, ...]` mit `len = w*h*4`. **Das ist der Pfad den Claude/MCP nutzt** weil ich die ganze RGBA-Sequenz auf einmal liefern kann.
4. **`SPRITE.SAVE(name$, path$)`** — schreibt das Sprite als PNG. Backend: `IMG_SavePNG()` aus SDL3_image (schon gelinkt). Path absolut oder relativ zum cwd.

### Sequenzen / Animation

Zwei Optionen, ich präferiere **A**:

- **A: Sprite-Sheet-PNG** — N Frames in NxM-Grid, ein PNG. Vorteil: ein File, schnell ladbar, das existierende `SPRITE.LOAD`-Sheet-Slicing kann unverändert weiterverwendet werden. Animation-Frames via `cols`-Feld plus `SPRITE.DRAW(name, x, y, frame)`.
  - Neuer Helper: `SPRITE.SAVE_SHEET(name$, cols, path$)` — speichert das Sprite-Internal als NxM-Sheet
- **B: Folder + Manifest** — `art/pacman/frame_0.png, frame_1.png, ...` + `manifest.json`. Flexibler, aber mehr Files und ein neues Load-Pfad.

### Wie Claude/MCP das benutzt

Workflow:
1. User: *"Mach einen 16x16 Pacman-Sprite, gelb mit schwarzem Maul rechts unten"*
2. Claude (via `jdb_eval`):
   ```basic
   SPRITE.CREATE "pacman", 16, 16
   DIM rgba = [...]  ' flat-Array 16*16*4 = 1024 Werte, von Claude berechnet
   SPRITE.SETBUFFER "pacman", rgba
   SPRITE.SAVE "pacman", "art/pacman.png"
   ```
3. Sprite ist auf Disk, kann via `SPRITE.LOAD "art/pacman.png"` in jedem Spiel verwendet werden

Für Animation: Claude berechnet 4 Frames Pacman (mund offen, halb-offen, geschlossen, halb-offen), packt sie in eine 64x16 RGBA-Buffer (4 Frames horizontal), ein `SPRITE.SETBUFFER` + `SPRITE.SAVE_SHEET "pacman_anim", 4, "art/pacman_anim.png"`.

### Tests

`tests/test_sprite_create_save_load.jdb`:
- CREATE → SETBUFFER → SAVE → LOAD-back → Pixel-Vergleich roundtrip
- Animation-Sheet roundtrip
- Edge-Cases: 1x1-Sprite, 256x256-Sprite, ungültiger Path → klarer Error

In `tests/native_test.jdb` als neue Sektion ergänzen (per Memory `feedback_run_tests_after_compile.md`) damit der Native-Pfad auch grün ist.

### Risiken

| Risiko | Mitigation |
|---|---|
| RGBA-Array-Layout-Mismatch (RGB vs BGR vs ARGB) | im Test explizit ein 2x2-Pattern mit bekannten Farben round-trippen, byte-genau prüfen |
| 64GB-OOM wenn Claude versehentlich `IOTA(2^31-1)` als RGBA-Quelle liefert | Defensive Path-Checks: `IF LEN(rgba) <> w*h*4 THEN ERROR` (siehe Memory `feedback_no_billion_element_ops.md`) |
| PNG-Compression-Level zu hoch → Save dauert sekunden | `IMG_SavePNG` Default ist ok, später evtl. Quality-Param |
| `SPRITE.SETBUFFER` blockt UI-Thread bei großen Sprites | Bulk-Move via `memcpy` direkt, nicht element-weise — schnell genug für ≤256x256 |

---

## Phase 3 — Live-Coding-Demo (später, recorded)

**Trigger:** Phase 1+2 grün, dokumentiert, mind. ein User hat damit ein Sprite erstellt.

**Format:** Recorded Live-Coding-Session (analog zur Stellar-Drift-Live-Tweak-Story die wir gerade in der Vibe-Coding-Stunde durchgespielt haben), ~20-30 min, später auf YouTube als eigener Cut zusätzlich zu den 14 Lessons.

**Story-Bogen:**
1. *"Lass uns ein zweites Spiel bauen, diesmal mit Sprites"*
2. Atomi tippt Game-Loop-Skeleton (5 min)
3. Atomi sagt zu Claude: *"Erstell mir einen 16x16 Pacman-Sprite + 4-Frame-Animation"* — Claude liefert via SPRITE.SETBUFFER, sichtbar auf dem Screen sofort
4. Weiter: Ghost, Kirsche, Wand-Tiles
5. 5 min später läuft ein spielbares Mini-Game mit Sprites die KEINER manuell gepixelt hat
6. Outro: *"Das ist der ganze Punkt von MCP-Pair-Coding — auch die Art wird kollaborativ"*

Game-Konzept-Optionen (zu entscheiden wenn's so weit ist):
- **Mini-PacMan** — klassisch, jeder versteht es sofort
- **Snake mit Sprites** — minimal aber gut für die Demo
- **Mini-Platformer** — anspruchsvoller, mehr Sprite-Bedarf, bessere Demo-Vielfalt

Sketch-Branding: *"Sprite-Quest"* oder *"Pixel-Pair"*. Final-Name beim Recording.

---

## Reihenfolge + Timing (Re-Plan 2026-05-21 — vorgezogen)

Launch ist auf Do 2026-06-04 verschoben. Phase 1+2 vor Launch.

**Diese Woche** (Do 2026-05-21 - So 2026-05-24):
- Do 2026-05-21: Phase 1 Refactor starten — `src/sprites.cpp` + `src/sprites.h` rausziehen, vm.cpp + llvm_codegen.cpp synchron, build-config
- Fr 2026-05-22: Phase 1 Pre-Commit-Gate, RPG-Demo + Stellar-Drift Sanity, lokaler Commit
- Sa-So 2026-05-23/24: NVIDIA-Linux-Build parallel auf der zweiten Maschine, Phase 2 Start

**Nächste Woche** (Mo 2026-05-25 - Do 2026-05-28):
- Mo: Phase 2 Builtins (CREATE / SETPIXEL / SETBUFFER / SAVE / SAVE_SHEET)
- Di: Phase 2 Tests + Docs (languages.md / help.txt / tmLanguage.json)
- Mi: Pre-Commit-Gate + RGBA-Roundtrip-Test grün, Sprite-Pipeline live für Recording
- Do-Fr: Video V2 Recording-Slot

**Launch-Woche** (Fr 2026-05-29 - Do 2026-06-04): wie im Hauptplan (V3-V5 Video + Tag 9-12).

**Phase 3** (Live-Coded Sprite-Game-Recording): post-launch, sobald Phase 1+2 stabil und mind. ein Public-User Feedback gegeben hat.

## Status

```
2026-05-21  Plan erstellt nach Vibe-Coding-Session mit Atomi + Sohn.
            Phase 1 entry-point: src/graphics.cpp Zeilen 149/163/212/220/1537.
2026-05-21  Vorgezogen — Atomi will Sprite-Welt VOR Launch in jdBasic.
            Launch +7 → Do 2026-06-04. Phase 1 startet heute.
```
