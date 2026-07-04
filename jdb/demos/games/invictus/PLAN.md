# INVICTUS - The Unbeatable Duel

A local PvP arena shooter in jdBasic. Two players, one screen, one question:
who is truly unbeatable? Dad (Tekken 138, Forsaken #2, Battlefront #1,
undefeated at table football) vs. Son. The game is the referee.

Working title: **INVICTUS** (alternatives: CROWN DUEL, LAST LEGEND, TILT ROYALE).
Deadline: **Sunday evening, playable and polished.**

---

## 1. Concept

Top-down twin-stick arena duel, shared screen (no splitscreen - both players
always visible, classic couch PvP). Fast rounds, lots of weapons, powerups,
destructible cover, multiple arenas from tight corridors to open field.

- Round = both players spawn with 3 lives. Last one standing takes the crown.
- First to 5 crowns wins the match and the on-screen title **UNBEATABLE**.
- Round timer 90s; on timeout the walls close in (sudden death shrink zone).
- Between rounds: 3-second winner taunt cam + announcer voice line.

Why this concept: it directly serves every requested ingredient (weapons,
powerups, cover, corridors, open field), it is balanced for two humans of
different ages, rounds are short so "one more round" never stops, and it is
achievable at high polish in one weekend.

## 2. Tech choice: SDL3 2D (GFX flag)

| Option | Verdict |
|---|---|
| **SDL3 2D (SPRITE/TILEMAP)** | **Chosen.** Proven stack (rpg demo, vibe-game-pack): sprite sheets + anims, tilemap collision, JOY gamepads, full SOUND DSP + MUSIC. Zero engine risk, all weekend goes into the game. |
| OpenGL | P1-P4 shipped, but no sprite/tilemap/collision layer - we would build an engine first. Wrong risk for a Sunday deadline. |
| Godot embed | Works (jd-one), but adds export/embed friction and splits debugging across two runtimes. Overkill for 2D couch PvP. |

Runtime: interpreter first (fast iteration, live-tweak via MCP). Native `-c`
kept as a performance escape hatch, so the code is written strict-friendly
from the start.

Resolution 1280x720, 32px tiles = 40x22 tile arenas, fixed camera, 60 FPS.

## 3. Game design

### Controls
- Twin-stick: left stick move, right stick aim, RT/R1 fire, LT dash, A pickup/swap.
- P1 gamepad, P2 gamepad (preferred) or keyboard fallback (WASD move,
  IJKL 8-way aim, Space fire, Shift dash).
- Dash: short burst + 0.2s invulnerability window. The skill move.

### Weapons (spawn as pickups on weapon pads, carry 1 + default)
| # | Weapon | Character |
|---|---|---|
| 0 | Blaster | default, infinite, mid fire rate |
| 1 | Shotgun | 6-pellet cone, corridor king |
| 2 | SMG | high rate, spray, cheap damage |
| 3 | Railgun | hitscan beam, pierces, long reload - open-field king |
| 4 | Rocket launcher | splash damage, destroys crates |
| 5 | Flamethrower | short cone, damage over time |
| 6 | Ricochet disc | bounces off walls up to 3x - trick shots around corners |
| 7 | Grenade | arcing bounce, 1s fuse, area denial |
| 8 | Mine layer | 3 proximity mines, faint blink |
| 9 | Energy sword | one-hit melee, huge dash synergy, high risk |

Ammo is limited per pickup; empty weapon reverts to Blaster. This forces
movement and keeps the map alive.

### Powerups (spawn on a timer at powerup pads, announcer calls them out)
- **Crown Star** - 5s full invincibility, announcer screams "UNBEATABLE!" (thematic centerpiece)
- **Overdrive** - double damage, red glow
- **Boots** - +40% speed
- **Shield** - absorbs 2 hits, visible bubble
- **Medkit** - +2 HP (max 5)
- **Ghost** - 6s at 25% alpha, hard to track

### Arenas (4 maps, ASCII-defined in source, tileset per theme)
1. **Open Field** - wide, sparse rocks, long sightlines. Railgun heaven.
2. **The Maze** - tight corridors, blind corners. Shotgun/disc territory.
3. **Crate Yard** - symmetric cover grid of destructible crates; cover erodes as the round runs.
4. **Colosseum** - open center ring surrounded by a corridor loop. Both playstyles at once.

Map select: winner of last round picks next arena (loser picks first).

### Damage model
5 HP, no regen. Blaster 1, shotgun pellet 1 (max 4 close), rail 3, rocket 3
(splash 2), flame 1/tick, disc 2, grenade 3, mine 2, sword 5. Numbers are
first guesses; Sunday afternoon is balancing time with the two test subjects.

## 4. AA polish plan (the "juice" layer)

- Screenshake scaled by weapon impact; hit-stop 40ms on kills.
- Particles: muzzle flash, shell casings, sparks on wall hits, smoke trails
  on rockets, crate splinters, pixel blood, death explosion + slow-mo 0.3s.
- Hit flash (white sprite blink), damage knockback.
- Announcer VO (Sorceress TTS): "ROUND ONE", "FIGHT", "HEADSHOT", "DOUBLE KILL",
  "UNBEATABLE", "SUDDEN DEATH", "CROWN POINT", plus one taunt per player name.
- Music: menu loop, 2 battle loops, sudden-death loop (tempo up). SOUND DSP
  sidechain ducking under announcer lines.
- Winner screen: crowned portrait, confetti particles, final stats
  (accuracy, favorite weapon, dashes, crowns).
- Attract/title screen with both fighter portraits and a VS lightning bolt.

## 5. Assets: Sorceress pipeline

Dad + Son generate on sorceress.games (that is the fun part for the crew);
exact prompts and specs below so exports drop straight in. Everything lands
in `assets/` as PNG (sprites/tiles) and WAV (audio).

| Asset | Spec | Sorceress tool |
|---|---|---|
| 2 fighter bodies | 32x32, 4-dir walk, 4 frames/dir = 4x4 sheet, transparent bg | Auto-Sprite v2 / True Pixel |
| Weapon sprites | 10x, ~24x12, side profile, transparent, drawn pointing right | True Pixel |
| Projectiles + FX | bullets, rocket, disc, grenade, mine, explosion sheet (6 frames 32x32) | Auto-Sprite v2 |
| 4 tilesets | 32x32 tiles: floor x3 variants, wall, crate, cracked crate, weapon pad, powerup pad | Tileset Forge |
| Powerup icons | 6x 24x24 | True Pixel |
| Portraits | 2x 128x128 for VS screen and winner screen | image model |
| SFX | per weapon shot + explosion, pickup, hit, death, dash, crown | Sound Studio |
| Music | menu, battle x2, sudden death; 8-bar loops, WAV | Sound Studio |
| Announcer VO | ~12 lines, deep arena voice | Sound Studio TTS |

Style anchor (put in every prompt): "16-bit pixel art, top-down arena shooter,
high contrast neon-on-dark palette, clean silhouette, game-ready sprite sheet,
transparent background".

**Nothing blocks on assets:** I build with procedural placeholders first
(generated via the devrig Python workshop with Pillow: colored capsule
fighters, geometric weapons, flat tilesets), same sizes and sheet layout as
the specs above. Real assets are a drop-in swap on Sunday.

### devrig (py MCP) asset pipeline duties
- Generate the complete placeholder asset pack (sheets, tiles, icons).
- Normalize Sorceress exports: trim, resize to spec, transparent-bg cleanup,
  re-grid sheets to 4x4 32px, palette sanity check.
- Batch-convert/trim audio exports to short WAVs.
- Saved as workspace helpers (py_savews) so re-running on new exports is one call.

## 6. File layout

```
jdb/demos/games/invictus/
  invictus.jdb        main game (single file, CLAUDE_LIVE pause pattern for live-tweaks)
  maps.jdb            arena ASCII maps + tile legend (IMPORTed module)
  assets/             png + wav (sorceress exports and placeholders)
  PLAN.md             this file
```

## 7. Schedule (honest estimate, not a 3-months-that-takes-3-hours one)

**Friday evening (~3h)** - agree plan; core loop running: movement, twin-stick
aim, blaster, tilemap collision, 2 players, 1 arena, placeholder assets, kills
and respawns. *Playable tonight.*

**Saturday (~6h)** - all 10 weapons, 6 powerups, 4 arenas, destructible crates,
rounds/crowns/match flow, dash, particles + screenshake, keyboard fallback.
Meanwhile the crew generates Sorceress assets from the spec table.
*Feature-complete Saturday night, first real duels.*

**Sunday (~4h)** - asset drop-in, music + SFX + announcer wiring, winner
screen + stats, balancing with live playtests (MCP live-tweak on a TWEAKS map,
no restarts), title screen. *Ship it, fight for the crown.*

## 8. Open questions

1. Gamepads: do we have two, or one pad + keyboard for P2?
2. Perspective confirmed top-down? (Alternative: side-view platform duel,
   Towerfall-style. Top-down fits corridors/open-field/cover better.)
3. Art direction: 16-bit neon pixel art proposed - veto from the Son?
4. Name: INVICTUS, or one of the alternatives, or something else entirely?
5. Sorceress account: free 100 credits enough to try, or go $49 lifetime?
