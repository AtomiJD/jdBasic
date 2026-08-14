---
name: jdvibe
description: Live-coding mode for jdBasic launch / demo / recording sessions. Locks Claude into a tight loop - pause, edit, recompile/resume - with zero chatter, zero build-advice, no script restarts, one-line "resumed - X" confirmations. **jdb_eval first**: most tweaks are a single MCP call against a tweakable map, not a file edit. Project-agnostic: works on any jdBasic script that follows the CLAUDE_LIVE pause pattern. Invoke once at the top of a session; the rules stay in effect until **Atomi** explicitly leaves the mode.
---

# /jdvibe - Recording-session live-coding

Activate this when **Atomi** is about to record a jdBasic launch / demo / pair-coding video where the viewer sees both the Claude terminal AND a running jdBasic window. The whole point is the tight live-tweak loop. Anything that breaks pacing breaks the video.

This mode is project-agnostic. It works for any jdBasic script that uses the `CLAUDE_LIVE` pause/resume pattern (`IMPORT CLAUDE_LIVE` next to the script - the C++ resolver searches one level deep into `modules/` and the script's own directory).

## Standing rules (in effect for the whole session)

1. **No build advice. No doc-checks. No "should I run tests?".** The setup is verified before record. Do what is asked.
2. **No multi-paragraph explanations.** Lead with the tool call. Words come after, in one short line, if at all.
3. **Tool order is the visual.** The viewer reads `jdb_eval` (or `jdb_stop` -> edit -> `jdb_recompile` -> `jdb_resume`) as the punchline. Make those visible. Don't bury them under preamble.
4. **Never restart the script.** The whole point is that the VM stays alive across edits. Use `jdb_eval` or `jdb_recompile`, never `jdb_load` once the project is running.
5. **If something goes wrong** mid-take, fix it in one follow-up edit. Don't apologise, don't explain, don't ask permission.
6. **Stay in English.** Atomi may speak German on camera; your terminal output is read by an international audience.

## Decision tree - which tool fires first

For every tweak ask: *can this be expressed as a one-line assignment to existing module state?*

```
   ┌─────────────────────────────────────────────────────────────┐
   │  Atomi: "Gib mir 100 Leben" / "Bullets doppelt so schnell" │
   └─────────────────────────────────────────────────────────────┘
                              │
            ┌─────────────────┴─────────────────┐
            │  Is the target a top-level DIM    │
            │  or a g_config MAP entry?         │
            └─────────────────┬─────────────────┘
                              │
                ┌─────── yes ─┴─ no ───────┐
                │                          │
                ▼                          ▼
   ┌─────────────────────┐   ┌──────────────────────────┐
   │ jdb_eval "X = N"    │   │ Read tiny slice → Edit  │
   │ jdb_resume          │   │ → jdb_recompile         │
   │ ~3-5 s total        │   │ → jdb_resume            │
   │ ONE round trip      │   │ ~20-40 s                │
   └─────────────────────┘   └──────────────────────────┘
```

`jdb_eval` runs the line in the live VM, persists immediately, no file touch, no recompile. The script's `running=0` STOP keeps the worker parked but module state is fully mutable from outside.

Fall back to `jdb_recompile` only when:
- The target is a `CONST` (true constants can't be re-bound at runtime).
- The change is inside a `FUNC` / `SUB` body (logic, not state).
- A new top-level `DIM` needs to come into existence.

## Project mutables - skip the Read

For the standard demos, the live-tweakable knobs live in a documented hot-spot. **Don't open the file just to look up where `lives` is** - use the cheatsheet below and go straight to `jdb_eval`.

### Stellar Drift - `jdb/demos/games/space_shooter.jdb`

| Atomi asks | jdb_eval line |
|---|---|
| "Gib mir N Leben"                 | `lives = N` |
| "Score auf N"                     | `score = N` |
| "Hi-Score auf N"                  | `hi_score = N` |
| "Spieler doppelt so schnell"      | `g_config{"player_speed_x"} = 11.0 : g_config{"player_speed_y"} = 9.0` |
| "Schießt schneller"               | `g_config{"fire_cd_normal"} = 3` (lower = faster, 9 default) |
| "Rapid noch schneller"            | `g_config{"fire_cd_rapid"} = 1` |
| "Bullets schneller"               | `g_config{"bullet_speed"} = -20.0` (negative = upward) |
| "Mehr Powerups"                   | `g_config{"powerup_drop_base"} = 0.5` (default 0.05) |
| "Unverwundbar"                    | `g_config{"invuln_hit_bare"} = 9999 : g_config{"invuln_hit_shield"} = 9999` |
| "Weniger Game-Over-Wartezeit"     | `g_config{"gameover_lock_dur"} = 30` |
| "Schild stärker"                  | `g_config{"shield_max"} = 999 : g_config{"shield_drain"} = 0.2` |
| "Schneller Level-Up"              | `g_config{"waves_per_level"} = 2` |
| "Boss jede Welle"                 | `g_config{"boss_every"} = 1` |
| "Härtere Gegner"                  | `g_config{"enemy_speed_mult"} = 1.4` |
| "Welle größer / langsamer / wilder" | `wave_amp = 200.0 : wave_speed = 0.8 : wave_freq = 0.04` |
| "Palette Cyber / Neon / Gold / Warm / Cool" | `apply_palette(4)` (1 cool, 2 warm, 3 neon, 4 cyber, 5 gold) |

All of these are `DIM` or `g_config` MAP entries - `jdb_eval` lands them in one MCP call, no file open.

### Generic state present in most demos

| State | How |
|---|---|
| score / lives / hi_score                  | top-level `DIM`, `jdb_eval "score = 0"` |
| paused / running flags                    | `jdb_eval "paused = 1"` (then resume) |
| wave / level counters                     | `jdb_eval "g_level = 5"` |
| palette / theme                           | usually a `apply_*(N)` SUB at top level |
| FUNC body changes (new behaviour, new enemy type, new physics) | file edit + `jdb_recompile` |

## Mode activation

When **Atomi** invokes `/jdvibe`, acknowledge with **one sentence**:

```
jdvibe mode active.
```

Nothing else. Do **not** guess at a project, do **not** mention specific games. Wait for Atomi to tell you what to load.

If Atomi says something like *"Let us code the shooter"* / *"Start the pacman, I want to code"* / *"Load games/parallax/parallax_game.jdb"*:

1. **Boot the project.** Call `mcp__jdbasic-stdio-win__jdb_load` with the path Atomi named. If only a project name was given (e.g. "the shooter"), use the cheatsheet below to pick - when in doubt, ask which file in one line.
2. **Wait.** Atomi plays for a few seconds, then presses **F6** in the project window. This calls `CLAUDE_LIVE.pause_pressed()` which triggers the script's `STOP` opcode; you'll see the script yield. **Do not interrupt.**
3. **Listen to the change.** Atomi describes a modification in chat in plain language.
4. **Find the matching cheatsheet row** for the current project. If present, fire `jdb_eval` immediately.
5. **Only if the change isn't in the cheatsheet**, read the relevant slice of the file (use `offset` + `limit` - never the whole file), edit, and `jdb_recompile`.
6. **Call `mcp__jdbasic-stdio-win__jdb_resume`** to release the worker.
7. **Print the one-line confirmation** and stop. Atomi takes over.

## What "done" looks like every loop

End every modification with one line in chat: `resumed - <one phrase>`. Examples:
```
resumed - 100 lives
resumed - rapid fire at cd=1, normal at cd=3
resumed - bullets cycle hues per frame
resumed - palette switched to cyber
resumed - powerup chance 10x
```
No more, no less.

## Tool defaults for /jdvibe

These are the only MCP tools you should reach for in this mode:

| Tool | When |
|------|------|
| `mcp__jdbasic-stdio-win__jdb_load`     | exactly once, at session start (when Atomi names a project) |
| `mcp__jdbasic-stdio-win__jdb_eval`     | **default for any state tweak** - one MCP call, persists in live VM |
| `mcp__jdbasic-stdio-win__jdb_recompile`| only when changing FUNC/SUB body or `CONST` |
| `mcp__jdbasic-stdio-win__jdb_resume`   | immediately after `jdb_eval` / `jdb_recompile` succeeds |
| `mcp__jdbasic-stdio-win__jdb_stop`     | only if F6 didn't fire (rare; the script's `running=0` handler usually does it) |
| `mcp__jdbasic-stdio-win__jdb_status`   | if Atomi explicitly asks "are we paused?" |

Anything else (`jdb_check`, `jdb_doc`, `jdb_funcs`, `jdb_vars`, `jdb_savews`) is **off** for this mode unless Atomi explicitly invokes it.

## Project file paths (just hints - Atomi names what he wants)

After the 2026-05-25 jdb/ restructure, demos live in `jdb/demos/<category>/` and the C++ resolver only searches one level deep (no walk-up), so each script's IMPORTs resolve against a sibling `modules/` or `jdb/modules/` only.

| Project | Likely file |
|---|---|
| "the shooter" / "stellar drift" | `jdb/demos/games/space_shooter/space_shooter.jdb` (dedicated project folder with own CLAUDE.md + Haiku model) |
| "pacman" / "the pacman"         | `release/jdbasic-vibe-game-pack-windows-x64/games/pacman/vibe_game.jdb` |
| "the parallax"                  | `jdb/parallax_game/parallax_game.jdb` |
| "the emulator" / "emu_run"      | `jdb/emu/emu_run.jdb` |
| "the snake"                     | `jdb/demos/games/snake_game.jdb` |
| "the tetris"                    | `jdb/demos/games/tetris_game.jdb` |
| "the raytracer"                 | `jdb/demos/games/raytracer.jdb` |
| "the chess"                     | `jdb/demos/games/chess_engine.jdb` |
| "an ai demo" / "the rag"        | `jdb/demos/ai/{ai_demo,ai_chat_demo,mini_llm,mini_onnx,rag_demo,classifier_demo}.jdb` |
| "the sequencer" / "sq"          | `jdb/demos/sound/sq_fluent.jdb` (full studio: `jdb/demos/gui/app_master.jdb`) |
| "a gl demo"                     | `jdb/demos/gl/gl_p{1..4}_*.jdb` |
| "the tui" / "tui demo"          | `jdb/demos/tui/tui_demo.jdb` |
| "the spreadsheet"               | `jdb/demos/gui/spreadsheet.jdb` |
| "vibe game" / "empty skeleton"  | `jdb/demos/games/vibe_game.jdb` |
| "the rpg" / "vallys reise"      | `fluppi/rpg_demo.jdb` |

These are hints, not authoritative - if Atomi names a file directly, use that. The `CLAUDE_LIVE` module is the pause/refocus/MOVE_WINDOW helper; it lives next to each consumer (`jdb/demos/games/claude_live.jdb`, `jdb/parallax_game/claude_live.jdb`, `jdb/tools/claude_live.jdb`). The `tools/winpos_probe.jdb` calibrator also imports it.

## Out-of-mode triggers

Exit `/jdvibe` mode (back to normal Claude behaviour) when:
- Atomi says "cut", "stop recording", "we're done", or any phrase that obviously ends the take.
- The script crashes hard (segfault, exit 139) and needs real debugging - the rules above forbid it, but a hard crash trumps the rules.
- Atomi asks for an explanation of *what* you just did. Then you can talk normally for one turn, then back to the loop.

## Setup checklist (Atomi runs this before pressing record)

Not your job during the take, but if any of these is missing the loop will stutter - flag it once at session start if you can tell from context:

- [ ] Wallpaper: `tmp/jdbasic_logo_4k.png`
- [ ] OBS scene: 1920x1080, Display Capture, 60 fps
- [ ] Claude terminal positioned on the clean monitor, project window will land via `CLAUDE_LIVE.MOVE_WINDOW_FROM_JSON` based on `jdb/winpos.json` (gitignored, per-user)
- [ ] Project script's F6-pause-resume loop pre-tested once
- [ ] Mic + desktop audio routed into OBS
- [ ] Full launch-video script: `release/launch_video_script.md` (gitignored, lives locally)
