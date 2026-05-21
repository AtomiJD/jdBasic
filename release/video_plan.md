# 90-Sekunden-Demo-Video — Unterplan
**Owner:** Atomi · **Erstellt:** 2026-05-19 · **Deadline:** Mi 2026-06-03 (Upload unlisted, public-Flip am Launch-Tag Do 2026-06-04)

> **Re-Plan 2026-05-21** — Video-Phasen V2-V5 alle +7 verschoben weil Launch nochmal eine Woche nach hinten ging (Sprite-Refactor wird vorgezogen, siehe [sprites_plan.md](sprites_plan.md)). Phase V1 (Skript) ist abgeschlossen. Recording-Slot ist jetzt Do-Fr 2026-05-28/29 statt 21/22.

## Warum eigener Plan

Das Video ist der eine Hebel an dem der ganze Launch hängt. Ohne überzeugendes Demo-Video versinkt der HN-Post in der zweiten Seite, die dev.to-Story verfehlt den Hook, und die Site-Konversion bleibt unter 5%. Mit überzeugendem Video kann jede dieser Plattformen 5-10x performen. Deshalb verdient das Video einen eigenen 8-Tage-Slot statt der ursprünglichen 3 Tage, plus einen expliziten Decision-Point ob Profi-Polish dazukommt.

## Was das Video erreichen muss

**In den ersten 5 Sekunden:** Klar machen, dass etwas Ungewöhnliches passiert. Game läuft, Pause-Overlay, Editor sichtbar — "wait, are they editing the game while it runs?"

**In den ersten 30 Sekunden:** Die Live-Tweak-Schleife einmal komplett zeigen. Game → Stop → Code-Change → Recompile → Resume → sichtbare Veränderung im Spiel.

**Am Ende (Sekunde 80-90):** Tagline, jdBasic.org, GitHub, Discord. Subtil. Keine Hard-Sell-Voice-Over.

**Was es NICHT zeigen muss:** Tensor-Math, Autodiff, REPL, AI-Tutorial. Eine Sache, sehr gut. Tiefe statt Breite.

---

## Phase V1 — Skript + Storyboard (Di-Mi 2026-05-19/20)
**Aufwand:** 4-6h

- **Beat-Sheet** in 10-Sekunden-Blöcken auf Papier oder Markdown
- **Voice-Over-Text** entscheiden:
  - **English** (max reach für HN/YT) — empfohlen
  - **Deutsch** mit englischen Untertiteln (Authentizität, aber halber Reach)
- **Live-narration vs pre-recorded voice-over** entscheiden — Letzteres erlaubt sauberes Pacing, Ersteres ist authentischer
- **Visuelle Assets sammeln** und vorbereiten:
  - jdBasic-Logo (Intro-Card, ~1s)
  - Lower-Third für `jdBasic.org` (Outro)
  - Tagline-Card (90s-Endframe)
  - Pause-Overlay-Variante mit höherem Kontrast für Recording (falls aktuelle zu subtil im Video)
- **Reference-Videos sammeln** (3-5 Stück die du als Look-und-Feel-Vorlage magst). Indie-Game-Trailer, Devlog-Highlights, Dev-Tool-Releases. Diese gehen in den Profi-Brief falls V4b.

**Deliverable:** `release/video_script.md` mit Beat-Sheet + Voice-Over-Text

## Phase V2 — Setup + Raw-Recording (Do-Fr 2026-05-28/29)
**Aufwand:** 4-6h verteilt auf 2 Sessions

- **OBS-Szene** definieren:
  - Variante A: Split-Screen (links Game, rechts Editor + MCP-Output)
  - Variante B: Picture-in-Picture (Game vollflächig, Editor schwebend in Ecke)
  - Variante C: Sequential (Game-Fullscreen → Cut → Editor-Fullscreen → Cut zurück)
  - Empfehlung: **A**, weil der Live-Tweak-Charakter sofort lesbar ist
- **Mic-Setup**: USB-Kondensator + Pop-Filter, Noise-Suppression über NVIDIA Broadcast oder RNNoise-Plugin in OBS
- **Recording-Specs**: 1080p60, NVENC oder x264 medium, Audio 48kHz mono
- **5-10 Takes pro Beat** — Atomi spielt durch, drückt STOP, editiert Color, recompiled, resumed, sichtbare Game-Reaktion
- **Backup-Captures separat**: Bare-Screen-Recording von nur Editor (für Insert-Shots) und nur Game (für Detail-Reactions)

**Deliverable:** `recordings/` Ordner mit 5-10 Takes, durchnummeriert, beste 2-3 markiert

## Phase V3 — Decision-Point: Eigen-Cut oder Profi? (Sa 2026-05-30)
**Aufwand:** 1-2h Rohcut + ehrliche Bewertung

- **Rough-Cut in DaVinci Resolve** (free) zusammenstecken: 90s, beste Takes, kein Color, kein Audio-Polish
- **Ehrlich testen**:
  - Hook in den ersten 5 Sekunden klar?
  - Live-Tweak-Moment unmissverständlich?
  - Pacing — keine Längen, keine Hektik?
  - Audio verständlich, kein Buzz/Klicks?
- **Entscheidung**:
  - JA, das trägt → **Phase V4a** (Self-Edit)
  - NEIN, Hook fehlt oder Pacing schwach → **Phase V4b** (Profi-Edit)
  - DRITTER WEG: Rough-Cut zeigt mir Atomi am Sonntag, ich gebe dritte Stimme dazu — dann gemeinsame Entscheidung
- **Wenn V4b gewählt**: Editor-Suche und Bezahlung anstoßen SOFORT, damit am Mo der Briefing-Call läuft

**Deliverable:** Rohcut + dokumentierte Entscheidung

## Phase V4a — Self-Edit (Sa-Mo 2026-05-30 / 06-01)
**Aufwand:** 8-10h verteilt auf 2-3 Sessions

- **DaVinci Resolve free** als Editor — keine Subscription, kommt mit Color/Audio-Tools
- **Cut**: beste Takes verlinken, Lückenfüllen mit Insert-Shots aus den Backup-Captures
- **Color**: Filmic-LUT (z.B. Resolve Color Managed → Output: Rec.709) + leichtes Sättigungs-Lift auf Game-Footage
- **Audio**:
  - Voice-Track: Compressor (Ratio 4:1, Attack 5ms, Release 50ms) → EQ (Low-Cut 80Hz, Lift 3-5kHz) → Limiter
  - Background-Music: lo-fi CC0 (FreeMusicArchive, ccMixter, pixabay-music) bei -18dB unter Voice
  - Game-SFX: -12dB unter Voice
- **Motion-Graphics minimal**:
  - Logo-Intro 1s mit Fade-In
  - Code-Highlight-Box bei Recompile-Moment (rechteckiger Outline-Pulse)
  - Outro-Card mit Site/Discord/GitHub
- **Captions/Untertitel** — Pflicht für HN-Crowd die ohne Audio scrollt. SRT-Datei mit DaVinci-Auto-Caption als Start, dann manuell korrigieren
- **Exports**:
  - Master: 1080p60 H.264 30Mbps (YouTube-Primary)
  - 9:16 1080x1920 (TikTok/YT-Shorts/Reels) — Crop und Re-Frame
  - 1:1 1080x1080 (Twitter/Bluesky/Mastodon) — Letterbox oder Re-Frame
  - 60s-Subset 16:9 für HN-Embed (HN bevorzugt kürzere Videos)

**Deliverable:** 4 finale Cuts in `release/video/` plus Original-Project-Datei

## Phase V4b — Profi-Edit (Sa-Di 2026-05-30 / 06-02)
**Aufwand:** 2-3h Briefing + 2-3 Iterationsrunden, **Budget 300-500€**

- **Editor-Suche** (in dieser Reihenfolge):
  1. **Fiverr "Top Rated Plus"** mit Tech-/Game-Showreel (Filter: Video-Editing, Game-Trailer-Editing). Zielen auf 200-400€ Tier, NICHT 50€-Tier.
  2. **Indie-Game-Trailer-Editoren auf X/Bluesky** — Suche nach "game trailer editor for hire". Die verstehen "Code on screen + Game on the other side" sofort. Erwarte 400-700€.
  3. **Discord "Pixel Art Group" oder "Indie Devs"** Server — viele freelance Editoren posten dort
- **NICHT empfohlen**: Generic Hochzeits-Videografen, AI-Auto-Edit-Tools (Pictory, Synthesia)
- **Brief vorbereiten** (alles in einem Drive/Dropbox-Ordner):
  - 3-5 Reference-Videos (was du magst, und WARUM)
  - Storyboard aus V1
  - Alle Raw-Takes aus V2 (mit "best take" markiert)
  - Voice-Over-Datei separat (oder Hinweis dass im Take drin)
  - jdBasic-Logo + Brand-Colors (#... aus Site-CSS)
  - Tagline + Endcard-Text
  - **WICHTIG**: 1-Paragraph-Erklärung was MCP ist und warum Live-Tweak besonders ist. Wenn Editor das nicht versteht, versteht das auch der Zuschauer nicht.
- **Discovery-Call vor Bezahlung** (15-30 min): zeige das Konzept, frage ihn was er versteht. Wenn er den "Wow-Moment" benennen kann → guter Editor. Wenn er nur "cool tech video" sagt → suche weiter.
- **Iterationsrunden** (typisch 2-3): V1 → Feedback → V2 → Feedback → Final. Mehr als 3 deutet auf Mismatch hin.
- **Liefer-Specs** wie V4a: 4 Cuts, je 16:9 / 9:16 / 1:1 / 60s-Subset

**Deliverable:** 4 finale Cuts (gleiche Specs wie V4a)

## Phase V5 — Upload + Embed + Site-Banner (Mi 2026-06-03)
**Aufwand:** 1-2h

- **YouTube upload unlisted** mit Title, Description, Tags, Custom-Thumbnail
- **Title**: `jdBasic + Claude: live pair-coding a real game` (oder Variante)
- **Description**: 3 Absätze + Links zu Site/GitHub/Discord/HN
- **Thumbnail**: 1280x720, Hero-Shot mit Pause-Overlay (kann aus den V1-Assets gerendert werden — bestehende `cards/thumb_*.png` Pipeline nutzen)
- **9:16 Variante separater YT-Short-Upload** (max-30s-Subset, separater Algorithmus)
- **Site-Banner** vorbereiten aber noch nicht aktivieren:
  - "Now live on YouTube" Banner-HTML in `/index.html`
  - Embed-iframe in `/ai-pair-coding/index.html`
  - Beides hinter Feature-Flag oder kommentiert, Aktivierung am Launch-Morgen
- **Bluesky/Mastodon-Posts** vorformulieren mit 1:1-Cut

**Deliverable:** Video live (unlisted) + alles fertig zum public-Flip am Donnerstag

---

## Decision-Matrix: Eigen-Edit vs Profi

| Kriterium | V4a Self | V4b Profi |
|---|---|---|
| Zeit-Investment Atomi | 8-10h | 2-3h Brief + Feedback |
| Out-of-Pocket | 0€ | 300-500€ |
| Authenticity | hoch (Solo-Dev-Feel) | mittel |
| Polish-Niveau | gut wenn DaVinci geübt | sehr gut |
| Risiko Hook trifft nicht | mittel | niedrig (gute Editoren wissen das) |
| Risiko Editor versteht MCP nicht | n/a | mittel — Discovery-Call abfangen |
| Iterations-Latenz | 0 (du selbst) | 24-48h pro Runde |
| Bestfall | "Indie-Devlog-Klasse" | "Indie-Game-Launch-Klasse" |
| Worstfall | "ehrlich-aber-amateurhaft" | "stylish-aber-Story-verfehlt" |

**Atomi's empfohlener Weg:** V1-V2-V3 selbst, dann V3-Decision auf Basis des eigenen Roh-Cuts:
- Wenn der eigene Cut den Hook trifft → V4a (du sparst Geld, behältst Authentizität)
- Wenn der eigene Cut den Hook nicht trifft → V4b mit klarem Brief und einem Editor der MCP zumindest grob versteht

**Anti-Empfehlung:** Profi für die GESAMTE Pipeline (Skript inklusive) anheuern. Du kennst die Story am besten. Ein Editor der die Story von Grund auf neu erzählt, verfehlt den Punkt warum gerade DIESES Video gemacht wird. Briefing + Raw-Takes bleiben bei dir.

## Risiken

| Risiko | Wahrscheinlichkeit | Mitigation |
|---|---|---|
| MCP-Recompile-Hang während Recording | mittel | Mehrere Takes, Worker-Resume-Fix ist drin seit 2026-05-17 |
| Audio-Buzz vom PC-Lüfter | mittel | Recording-Zeit wählen wenn Build nicht läuft, oder USB-Mic mit Richtcharakteristik |
| Atomi nicht zufrieden mit eigenem Cut, kein Profi-Slot mehr frei | niedrig wenn V3-Decision Sa | Profi-Suche bei Bedarf VOR V2 starten, parallel zum eigenen Recording |
| Profi versteht Live-Tweak nicht, liefert generisches Tech-Video | mittel | Discovery-Call BEFORE Anzahlung, kein Editor der nicht den "Wow-Moment" benennen kann |
| Voice-Over-Take wackelig (Versprecher, falsche Betonung) | hoch | Pre-recorded VoiceOver vorbereiten falls live-narration nicht in 5 Takes sitzt |
| Pause-Overlay zu subtil im Video | niedrig | High-Contrast-Variante vorbereiten, ggf. recompile mit angepasstem Overlay vor V2 |

## Was Claude/MCP übernimmt

- **V1 Skript-Beat-Sheet** kann ich draften — du gibst die Beats, ich strukturiere
- **V1 Voice-Over-Text** kann ich draften in DE+EN parallel
- **V4a Caption-Refinement**: SRT-Datei können wir gemeinsam durchgehen
- **V4b Editor-Brief**: ich draft den Brief, du editierst und schickst
- **V5 Thumbnail-Rendering**: die existierende `cards/`-Pipeline kann ich auf neue Frames anpassen
- **V5 Bluesky/Mastodon-Posts**: Drafts für alle Plattformen

## Status-Tracking

Tagesnotizen unten anhängen wenn du eine Phase startest oder abschließt.

```
2026-05-19 — Plan erstellt, Phase V1 startet
```
