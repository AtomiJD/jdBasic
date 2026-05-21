# jdBasic Public-Launch — 4-Wochen-Plan
**Start:** 2026-05-10 (So, Muttertag) · **Launch-Tag:** 2026-06-04 (Do) · **Retrospektive:** 2026-06-06 (Sa)

> **Re-Plan 2026-05-19** — Launch um eine Woche verschoben (auf 2026-05-28). Grund: Video ist der kritische Hebel und braucht echte Zeit (siehe [video_plan.md](video_plan.md)). Tag 1 Strix-Halo-Linux-Build steht (am 2026-05-11 erledigt), NVIDIA-Linux-Build am Wochenende 2026-05-23/24. Discord (Tag 8) ist live seit 2026-05-17. Pakete `core` + `mcp-native` Build 66 liegen seit 2026-05-18 im `release/`.

> **Re-Plan 2026-05-21** — Launch nochmal eine Woche verschoben (jetzt 2026-06-04). Grund: Sprite-Refactor + AI-Sprite-Builtins (siehe [sprites_plan.md](sprites_plan.md)) werden VOR Launch eingebaut, damit sie Teil der ersten öffentlichen Release-Story sind. Phase 1+2 belegen ~10-12h aus dieser Woche.

## Ziel

jdBasic + MCP-Pair-Coding sichtbar machen. Drei Builds parallel scharfziehen (Windows x64, Linux x64 NVIDIA, Linux x64 Strix Halo / AMD Radeon 8060S iGPU), die Site um eine "AI-Pair Coding"-Subseite erweitern, ein 60-90-Sekunden-Demo-Video produzieren, Discord-Server aufstellen, und auf HN + dev.to + Reddit + Programming-Languages-Discord launchen.

## Beteiligte

- **Atomi** — ~30-40h/Woche
- **Helper** — 2-5h/Woche, parallele Aufgaben mit niedrigem Kontext-Load
- **Claude (via MCP)** — Drafts (Texte, HTML, Code), Verfügbar bei Bedarf

## Plattform-Notiz

Beide Linux-Maschinen sind **x86_64**:
- NVIDIA-Box: bisheriger Linux-Port-Stand, 3 Wochen Commits hintenan
- Strix Halo (AMD Ryzen AI Max+ 395): 16 Zen-5-Kerne, Radeon 8060S iGPU mit 40 RDNA-3.5-CUs. Vulkan über Mesa RADV erwartet, ROCm optional für ONNX. Kein ARM-Cross-Compile nötig.

---

## Week 1 — Substanz aufbauen

### Tag 1 — So 2026-05-10 — Linux-Catchup auf Strix Halo  ✓ Strix done, NVIDIA pending
**Atomi (~6-8h)** — *nach Muttertag-Verpflichtungen*
- Repo pullen, 3 Wochen Commits durchziehen
- `build.sh GFX IMGUI NATIVEC HTTP` smoke
- libs/ Stand prüfen (SDL3, IMGUI, LLVM, OpenSSL für Linux)
- Pre-commit-Gate auf Linux: 4 suites × interp+native + rpg/emu smoke
- **Quick-Win**: `linux-x64-amd-strixhalo` Build-Artefakt im `release/` ablegen, danach NVIDIA-Box-Variante `linux-x64-nvidia`

**Status 2026-05-19**
- ✓ Strix-Halo-Build (`linux-x64-amd-strixhalo`) liegt im `release/`
- 🔲 NVIDIA-Variante eingeplant für Wochenende **Sa-So 2026-05-23/24** — separater Slot, blockiert nicht den kritischen Pfad

**Risiko**
- SDL3 + IMGUI-SDL3-Backend auf RDNA 3.5 + Mesa RADV — wenn Vulkan-Fehler, fallback auf Software-Renderer für die Smoke-Tests, Fix später
- ONNX/ROCm: optional, NICHT auf den kritischen Pfad heute

### Tag 2 — Mo 2026-05-11 — Live-Tweak Stabilisierung
**Atomi (~5h)**
- `GFX.PLOT_POINTS`-Lifetime-Bug fixen — verdacht: renderer-side async-submit der den Caller-Buffer nicht kopiert. Per-frame allocation in `draw_stars` reicht heute aus, um SDL aufzuhängen.
- Stress-Test-Skript schreiben: 20× hintereinander STOP → recompile → resume in einer Schleife, kein Hang
- `mcp-runtime/jdBasic.exe.old` weg, README daneben mit Setup-Anleitung

**Helper (~2h)**
- Spec für "Live-Tweak Reliability Suite" reviewen, Manuell durchspielen, Bugreport nach Schema

### Tag 3 — Di 2026-05-12 — MCP-Subseite auf jdBasic.org
**Atomi (~5h)**
- Neue Seite `/ai-pair-coding/index.html` analog zum bestehenden Tutorial-Style (Tailwind, gleicher Header)
- Sections:
  - "Why MCP" (Hook + Demo-Video-Platzhalter)
  - "Setup" (.mcp.json snippet zum kopieren)
  - "5-Min Walkthrough" (jdb_load → STOP → eval → recompile → resume)
  - "Tool Reference" (jdb_load / jdb_eval / jdb_resume / jdb_stop / jdb_recompile / jdb_status / jdb_vars / jdb_funcs / jdb_check / jdb_doc)
- Verlinkung im Hauptmenü + 7. Feature-Card "AI-Pair Coding" auf der Landing-Page

**Helper (~1h)**
- Bestehende Tutorialseiten review: Stale-Links, veraltete Code-Snippets, Konsistenz mit aktuellem Stack

### Tag 4-6 — **VIDEO BLOCK** — eigener Unterplan
*Mi 2026-05-13 - Mi 2026-05-27 (zwei Wochen statt drei Tage)*

**Re-Plan:** Das 90s-Video ist der eine Hebel, an dem der ganze Launch hängt. Selbstgebastelt-und-schnell verliert auf HN/YT-Frontpage gegen jeden anderen polierten Tech-Drop. Der ursprüngliche 3-Tage-Block (Skript/Rec/Cut) wird zu einem dedizierten Unterplan mit Decision-Point ob Eigen-Edit oder Profi-Polish.

**Vollständiger Plan: [video_plan.md](video_plan.md)** — fünf Phasen V1-V5, mit Profi-Fallback ab V4.

Kurzfassung der Phasen:
- **V1 — Skript** (Di-Mi 2026-05-19/20)
- **V2 — Setup + Raw-Takes** (Do-Fr 2026-05-21/22)
- **V3 — Decision-Point: Eigen-Cut oder Profi?** (Sa 2026-05-23)
- **V4a — Self-Edit** ODER **V4b — Profi-Edit** (Sa 2026-05-23 - Di 2026-05-26)
- **V5 — Upload + Embed + Cuts** (Mi 2026-05-27)

### Tag 7 — Sa 2026-05-16 — Buffer/Rest  ✓ verbraucht
- Discord-Skeleton fertig, Strix-Halo-Linux-Build gestempelt

---

## Week 2 — Polish + Launch

### Tag 8 — So 2026-05-17 — Discord-Server  ✓ done
**Atomi (~3h)**
- Server `jdBasic`
- Channels: `#welcome`, `#announcements`, `#general`, `#help`, `#showcase`, `#ai-pair-coding`, `#contributing`, `#linux`, `#off-topic`
- Roles: `@maintainer`, `@helper`, `@active`, `@beta-tester`
- Welcome-Bot mit Pinned-Message: TL;DR + Try-in-Browser + GitHub
- Permanenter Invite-Link

**Helper (~1h)**
- Server-Banner-Grafik (Logo, Farben matching der Site)

### Tag 9 — Mo 2026-06-01 — Article Drafting (Claude unterstützt)
**Atomi (~5h)**
- **dev.to article** (~1500 Wörter): "How I built a BASIC dialect that Claude can live-code with you"
  - Hook: 90s Demo-Video embedded
  - Story: warum BASIC + warum MCP + technische Architektur (worker thread, STOP/RESUME, recompile)
  - Code-Snippets aus space_shooter
  - Try-it Link, GitHub, Discord
- **HN-Post** (~600 Wörter):
  - Title: `Show HN: jdBasic – a modern BASIC with built-in autodiff and live MCP pair-coding`
  - Body: Was es ist, was neu, ehrliche Limitations

### Tag 10 — Di 2026-06-02 — Dry-Run + Bug-Hunt
**Atomi (~4h)**
- Auf einer FRISCHEN Windows-Box (oder VM): `jdbasic-mcp-native-windows-x64.zip` runterladen, .mcp.json setup, space_shooter.jdb live-tweaken
- Stoppuhr: Onboarding-Zeit unter 10min?
- Rough-Edges-Liste → fix oder dokumentieren

**Helper (~2h)**
- Dasselbe auf seiner Maschine: "blind onboarding test", Helper hat noch nie MCP+jdBasic genutzt → ehrliches Gauge

### Tag 11 — Mi 2026-06-03 — Final-Polish + Cross-Posting Prep
**Atomi (~4h)**
- HN-Post finalisieren basierend auf Helper-Feedback
- YouTube-Video auf public flippen, embed-Code für Site
- Site-Update: Banner "Now live on YouTube" mit Video-Embed auf Landing
- Optional: Lobste.rs Invite checken; sonst Fallback auf r/programming, r/programminglanguages, r/gamedev

### Tag 12 — Do 2026-06-04 — **LAUNCH**
**Atomi (4-6h aktive Präsenz)** — *Beste Zeit: Di-Do 7-9am EST = 13-15 Uhr CET*

| Zeit (EST / CET) | Aktion |
|---|---|
| 08:00 / 14:00 | HN "Show HN" Post |
| 08:30 / 14:30 | dev.to Artikel publish, mit Cross-Link zum HN-Thread |
| 09:00 / 15:00 | Tweet/Bluesky/Mastodon-Threads mit 90s-Video |
| 09:30 / 15:30 | Discord "Programming Languages" + "hey-look" |
| 10:00 / 16:00 | r/programminglanguages, r/programming |
| (laufend) | Eigenes Discord: `#announcements` mit HN-Link |
| (ganzer Tag) | **Aktiv auf HN antworten** — schnelles, tiefes Antworten innerhalb der ersten 2h hebt den Score signifikant |

**Helper (~1h)**
- Quick-FAQ in `#help` basierend auf den ersten Community-Fragen

### Tag 13 — Fr 2026-06-05 — Iterate + Respond
**Atomi (~4h)**
- Späte Kommentare/Issues/PRs
- Bug-Reports triage: kritische direkt, andere als GitHub-Issue
- Discord-Mod: Spam/Off-topic raushalten

### Tag 14 — Sa 2026-06-06 — Retrospektive
**Atomi (~2h)**
- Metrics: HN-Score, dev.to-Reads, YouTube-Views, Discord-Member, GitHub-Stars-Delta
- 1-Monats-Followup-Plan: Top-3-Wünsche aus der Community (erwartet: Linux-Builds, mehr Tutorials, ein konkreter Feature-Wunsch)

---

## Was wegfällt wenn Zeit knapp wird (in dieser Reihenfolge)

1. **dev.to-Artikel** — eine Woche nach HN nachreichen
2. **Bluesky/Mastodon-Posts** — Twitter + HN reichen
3. **AI-Tutorial-Refresh** (Tag 4) — wenn Tensor-Stack stabil, nur Tutorial-Code testen, nicht umschreiben
4. **Site-Banner mit Video** — kann nach HN
5. **Lobste.rs** — ohne Invite einfach skippen

## Was Claude/MCP übernehmen kann

- **HN-Post + dev.to-Artikel** drafte ich (Tag 9-10), du editierst dann
- **Demo-Skript** detailliert (Tag 4) — du musst nur sprechen + filmen
- **Discord-Welcome-Bot-Texte + FAQ** vorab
- **Site-Subseite "AI-Pair Coding"** kann ich als HTML rüberreichen, du klickst's ins Tailwind-Template

## Erfolgs-Metriken

**Konservativ (Floor)** — wenn alles solide aber ohne viralen Effekt:
- HN: ~80 Punkte, ~25 Kommentare
- dev.to: ~500 Reads erste Woche
- YouTube: ~200 Views erste Woche
- Discord: ~50 Member
- GitHub-Stars: +50 in 2 Wochen

**Realistic (P50)** — wenn die Story durchkommt:
- HN: 200-400 Punkte, 100+ Kommentare
- dev.to: 2-5k Reads erste Woche
- YouTube: 1-3k Views erste Woche
- Discord: 200-500 Member
- GitHub-Stars: +200-500 in 2 Wochen

**Stretch (P90)** — wenn HN-Front-Page hält + viraler Sekundär-Effekt:
- HN: 600+ Punkte, Front-Page mehrere Stunden
- dev.to: 10k+ Reads
- YouTube: 5-10k Views erste Woche
- Discord: 1000+ Member
- GitHub-Stars: +1000-2500 in 2 Wochen

---

## Daily Standup-Format (3 Minuten, asynchron via Discord oder kurzer Bash-Note)

```
**$DATE**
- Done: ...
- Today: ...
- Blocked: ... (oder "no")
- Helper-task ready: ... (oder "no")
```

Dieses Format reduziert Koordinations-Overhead, lässt sich auch beim Helper als 2-Zeiler im Discord-Channel `#contributing` posten.
