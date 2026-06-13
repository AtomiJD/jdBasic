# WM 2026 Dashboard - Plan (GFX / ImGui)

A full FIFA World Cup 2026 dashboard in jdBasic GFX+ImGui. Data from the ESPN
hidden JSON API (no key). This is a multi-phase weekend project.

## 1. Data sources (ESPN, no key, JSON)

| Endpoint | Provides | Refresh |
|---|---|---|
| `/teams` | all 48 nations: id, name, code, color, flag URL | once/session |
| `/teams/{id}` | detail: colors, logos, record, group, nextEvent | lazy, on-select |
| `/scoreboard?dates=A-B` | matches in range: score, status, venue, `details[]` (goals/cards), team colors | live 30-60 s |
| `/apis/v2/.../standings` | `children[]` = groups A-L, each `entries[]` with GP/W/D/L/GF/GA/GD/Pts + flag | 2-5 min |
| `/summary?event={id}` | the gold (~428 KB): `boxscore` (team stats), `rosters` (lineups + headshots), events, `gameInfo` (venue/attendance/referee), H2H, last-5, `leaders` | lazy; live 20-30 s |

Base: `https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/...`
(standings is under `/apis/v2/sports/soccer/fifa.world/standings`).

## 2. Images (flags + headshots)

- Flags: `team.logos[0].href` -> `.../countries/500/ger.png`
- Headshots: `rosters[].roster[].athlete.headshot.href`
- Pipeline: `HTTP.GET$(url)` -> bytes via `BINWRITER` to `cache/flag_GER.png`
  -> `SPRITE.LOAD` -> texture id (cache one per image).
- BLOCKER: there is no `GUI.IMAGE` builtin yet -> textures can't be drawn
  inside ImGui windows. Two options:
  - (A, recommended) add a small C++ builtin `GUI.IMAGE(sprite_id, w, h)`
    wrapping `ImGui::Image` over the SDL texture `SPRITE.LOAD` already creates
    (~1 file in `gui.cpp`). Flags/headshots then render everywhere, including
    scrolling tables/lists.
  - (B, no C++) draw flags only in fixed regions via `SPRITE.DRAW` over the
    canvas (match header, live card); tables use coloured 3-letter codes.

## 3. Tech stack

- Build flags: `GFX IMGUI HTTP` (+ existing). Window via `SCREEN`.
- Async HTTP (`HTTP.GET_ASYNC$` + `THREAD.ISDONE` / `THREAD.GETRESULT`) is
  mandatory so the 60-fps UI never blocks on a fetch.
- ImGui surface (all present): `GUI.BEGIN/END/BEGIN_CHILD`,
  `BEGIN_TAB_BAR/ITEM`, `BEGIN_TABLE` (sortable), `SELECTABLE`, `COMBO`,
  `PROGRESS` (stat bars), `TREE_NODE`, `SEPARATOR_TEXT`, `COLUMNS`.

## 4. Architecture

```
[Data]   async fetch -> JSON.PARSE -> in-memory cache (MAPs/Arrays)
   gTeams[], gStandings, gSchedule[], gSummaries{eventId->...}, gImg{code->spriteId}
[Refresh] timers: live 20-30 s | scoreboard 60 s | standings 5 min
[UI]     immediate-mode render each frame from cache; never block in a frame
```

## 5. UI layout

```
+----------------------------------------------------------------------+
| WM 2026   12.06 14:30   [LIVE: GER 1-0 CUW 67']         [Refresh]    |
+------------------+---------------------------------------------------+
| NAV              |  CONTENT (per selection)                          |
|  Overview        |                                                   |
|  Nations (48)    |                                                   |
|  Groups A-L      |                                                   |
|  Schedule        |                                                   |
|  Live            |                                                   |
|  Stats           |                                                   |
+------------------+---------------------------------------------------+
```

Views:
1. **Overview** - live card (if any) + today's matches + next GER match + group leaders.
2. **Nations** - grid of 48 with flag+name; click -> Team Detail.
3. **Team Detail** - flag, colors, record, group rank, full schedule (results + upcoming), squad/form.
4. **Groups/Tables** - 12 sortable `GUI.BEGIN_TABLE` (flag, GP, W, D, L, GF, GA, GD, Pts), qualification spots highlighted.
5. **Schedule** - all 104 matches by matchday/date, Combo filter by team/group; click -> Match Detail.
6. **Match Detail** - header (both flags, score, status, venue, attendance, referee) + tabs:
   - Summary: event timeline (goals/cards/subs with minute + player + headshot)
   - Stats: `boxscore` as comparison bars (possession/shots/fouls/corners/cards via `GUI.PROGRESS`)
   - Lineups: starting XI + bench per team (no/pos/name/headshot); optional formation pitch
   - H2H & Form: head-to-head + last 5
7. **Live** - big live card: running clock, live score, live stats, event feed, auto-refresh 20-30 s, pulse indicator.
8. **Stats** - top scorers / assists / cards tournament-wide (from `leaders`).

## 6. Phases

| Phase | Content |
|---|---|
| 0 | Skeleton: GFX+ImGui window, nav+content layout, async fetch+cache, build `GUI.IMAGE` + verify image pipeline |
| 1 | Groups/Tables + Nations grid (flags) |
| 2 | Schedule + Match Detail (summary events + boxscore stats) |
| 3 | Team Detail + Lineups + headshots |
| 4 | Live ticker + auto-refresh + Stats/Leaders |
| 5 | Polish: formation pitch, H2H, animations, team-colour accents |

## 7. Phase 0 - make-or-break to verify/build first

1. `GUI.IMAGE` builtin (C++, gui.cpp) - the flag enabler. Recommended: build it.
2. Binary download: is `HTTP.GET$` binary-safe (PNG bytes)? else a download-to-file path. -> image pipeline.
3. Clean async pattern: `HTTP.GET_ASYNC$` + poll per frame.

## Notes

- Verified live (2026-06): all endpoints return data; standings 169 KB,
  summary 428 KB; competitor/team `color` is inline in scoreboard + standings;
  rosters carry headshot URLs; flags at `logos[0].href`.
- Single-line cmd ticker already exists: `jdb/demos/web/wm_ticker.jdb`.
