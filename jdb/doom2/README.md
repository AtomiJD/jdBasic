# DOOM in jdBasic

A file-by-file port of linuxdoom-1.10 to jdBasic, compiled with `-c`. The play
simulation is deterministic, so the demos inside the WAD serve as its test: a
demo that runs in sync to its end proves the game logic.

Tracker: jdTrakr project `doom` (epic #967, one ticket per phase).

## Rules of the port

- Same algorithms as the C source, one module per C file, same function names.
  Modules import each other in circles where the C files call each other.
- Where C hands a function to an iterator (blockmap lines and things, path
  intercepts, thinkers), the caller names it by a number and the iterator
  calls the function of that number.
- `fixed_t` is 16.16 fixed point in an `INTEGER`, kept in the signed 32-bit
  range. `angle_t` is a 32-bit binary angle, masked with `M_FIXED.ANGLE_MASK`
  after every addition and subtraction.
- A C struct is one array per field (`sector_floorheight[]`, `line_flags[]`).
  A C pointer is an index, and -1 stands for NULL.
- Lump data lives in `W_WAD.pool`, one byte per cell. What the C source calls
  a pointer into a lump is a place in the pool.
- The frame path reads flat arrays only: no strings, no map lookups. Names
  (textures, flats, sprites) become numbers when a level loads.
- The framebuffer holds palette indices; the palette is applied when the frame
  is shown.
- `tables.jdb` and `info.jdb` are generated from the C source by
  `tools/gen_data.jdb` and are never edited by hand.

## Rules the compiler asks for

- Every module carries `OPTION "EXPLICIT"` and `OPTION "STRICT"`.
- **An array cell is a FLOAT64 in a compiled program.** Arithmetic written
  directly on cells is double arithmetic, and a product above 2^53 (every
  `FixedMul` of large values) loses its low bits there. A cell that takes
  part in such arithmetic is read into an `INTEGER` first: by a declaration
  (`DIM w AS INTEGER = tex_width[t]`), or through `CLNG()` in an assignment
  or inside an expression. STRICT reports an assignment that lacks the
  conversion. Cell to cell (`a[i] = b[j]`), cells as indexes and cells as
  arguments of a `FUNC` with `AS INTEGER` parameters need nothing. The
  declaration form needs a jdBasic with the fix of trakr jdbasic #979
  (commit d7f4304b or later).
- A cross-module `FUNC` call costs about 20 ns compiled, the same arithmetic
  inline about 5 ns. Inner loops over pixels write `FixedMul` out; everything
  else calls it.
- Compiled, a builtin of the runtime library copies its string arguments, so
  `UNPACK` with an offset into the 13 MB WAD string costs about 4 ms a call.
  `w_wad.jdb` caches a lump byte by byte with `BYTEAT`, which is quick in
  both backends, and uses `UNPACK` only for the eleven lumps of a level. No
  other module touches the WAD string.

## What is needed

- `doom.wad` of The Ultimate Doom 1.9, the DOS release (12,408,292 bytes,
  not in the repository). It is looked for in the folder the environment
  variable `DOOMWADDIR` names, else in `jdb/doom/wad/classic/`. The numbers
  in the tests belong to this file. The re-release IWAD (12,996,515 bytes)
  has the same Episode 1 and the same demos but full-screen pictures 560
  pixels wide; `V_DrawPatch` shows the middle 320 columns of such a patch.
- `jdb/doom/linuxdoom-1.10/` (the C source, not in the repository) only to run
  the generator again.

## Modules

| Module | C file | Content |
|---|---|---|
| `d_main.jdb` | d_main.c | where the IWAD is found (`IdentifyVersion$`) |
| `doomdef.jdb` | doomdef.h, doomdata.h, m_bbox.h, p_local.h | screen size, lump order, line flags, box sides, blockmap units |
| `m_fixed.jdb` | m_fixed.c, tables.h | `FixedMul`, `FixedDiv`, `ToS32`, angle constants |
| `m_random.jdb` | m_random.c | `P_Random`, `M_Random` |
| `tables.jdb` | tables.c, v_video.c | `finetangent`, `finesine`, `tantoangle`, `gammatable` (generated) |
| `info.jdb` | info.c, info.h, sounds.h | states, thing types, sprite names, enums (generated) |
| `i_system.jdb` | i_system.c | `I_Error` |
| `w_wad.jdb` | w_wad.c, z_zone.c | WAD directory, lump cache (`pool`), `W_CacheLumpNum` |
| `r_data.jdb` | r_data.c | textures and `R_GetColumn`, flats, sprite lump sizes, colormaps |
| `p_setup.jdb` | p_setup.c, r_state.h | level loader and the level's map arrays |
| `v_video.jdb` | v_video.c | the five screens, `V_DrawPatch`, `V_CopyRect`, `V_DrawBlock` |
| `i_video.jdb` | i_video.c | window, palette, `I_FinishUpdate` |
| `r_main.jdb` | r_main.c | view setup, view and light tables, `R_PointToAngle`, `R_PointOnSide`, `R_RenderPlayerView` |
| `r_bsp.jdb` | r_bsp.c | BSP walk, clipping against solid walls, drawsegs |
| `r_segs.jdb` | r_segs.c | `R_StoreWallRange`, `R_RenderSegLoop`, `R_RenderMaskedSegRange` |
| `r_plane.jdb` | r_plane.c | visplanes, spans, sky, the clip arrays |
| `r_draw.jdb` | r_draw.c | `R_DrawColumn`, `R_DrawSpan`, shadow and translated columns, the view window |
| `r_sky.jdb` | r_sky.c | sky flat and texture |
| `r_things.jdb` | r_things.c | sprite frames and rotations, vissprites, the masked pass (`R_DrawMasked`), the weapon sprites |
| `doomstat.jdb` | doomstat.h, g_game.c | game state: skill, episode, map, players in game, starts |
| `d_player.jdb` | d_player.h, d_ticcmd.h | the players, one array per field of player_t |
| `g_game.jdb` | g_game.c | `G_BuildTiccmd`, `G_InitNew`, `G_DoLoadLevel`, `G_PlayerReborn`, `G_Ticker` (the tic only) |
| `p_tick.jdb` | p_tick.c | thinkers, `P_Ticker` |
| `p_mobj.jdb` | p_mobj.c, p_mobj.h | things: spawn, remove, states, movement in x, y and z |
| `p_maputl.jdb` | p_maputl.c | sides and distances, line openings, thing links, blockmap iterators, `P_PathTraverse` |
| `p_map.jdb` | p_map.c | `P_CheckPosition`, `P_TryMove`, `P_SlideMove`, `P_UseLines` (movement half) |
| `p_user.jdb` | p_user.c | `P_PlayerThink`, movement by ticcmd, view height and bob |

Not ported yet. These modules exist with the functions other modules call,
and the functions do nothing:

| Module | C file | Functions | Phase |
|---|---|---|---|
| `p_spec.jdb` | p_spec.c | `P_CrossSpecialLine`, `P_PlayerInSpecialSector`, `P_UpdateSpecials`, `P_SpawnSpecials` | P5 |
| `p_switch.jdb` | p_switch.c | `P_UseSpecialLine` | P5 |
| `p_inter.jdb` | p_inter.c | `P_DamageMobj`, `P_TouchSpecialThing` | P6 |
| `p_enemy.jdb` | p_enemy.c | `A_Call` (the action of a state) | P6 |
| `p_pspr.jdb` | p_pspr.c | `P_SetupPsprites`, `P_MovePsprites` | P6 |
| `s_sound.jdb` | s_sound.c | `S_StartSound`, `S_StopSound` | P8 |

Left out of ported files until their callees exist: the shooting half of
p_map.c and `P_SpawnMissile`, `P_SpawnPlayerMissile` of p_mobj.c (P6).

## Running the checks

From the repository root, each test interpreted and compiled:

```
build/jdBasic.exe jdb/doom2/test_p0.jdb
build/jdBasic.exe -c jdb/doom2/test_p0.jdb && jdb/doom2/test_p0.exe
build/jdBasic.exe jdb/doom2/test_p1.jdb
build/jdBasic.exe -c jdb/doom2/test_p1.jdb && jdb/doom2/test_p1.exe
build/jdBasic.exe jdb/doom2/test_p2.jdb
build/jdBasic.exe -c jdb/doom2/test_p2.jdb && jdb/doom2/test_p2.exe
build/jdBasic.exe jdb/doom2/test_p3.jdb
build/jdBasic.exe -c jdb/doom2/test_p3.jdb && jdb/doom2/test_p3.exe
build/jdBasic.exe jdb/doom2/test_p4.jdb
build/jdBasic.exe -c jdb/doom2/test_p4.jdb && jdb/doom2/test_p4.exe
```

`test_p0` covers fixed point, random numbers and the generated tables.
`test_p1` loads E1M1 and compares what the modules hold with numbers
`tools/ref_p1.jdb` computed from the WAD file on a path of its own.
`test_p2` checks the view tables and the angle arithmetic and renders frames
of E1M1: each has to cover every pixel of the view and to match the hash
recorded for it, interpreted and compiled alike.
`test_p3` checks the sprite tables against the names of the sprite lumps,
the translation tables and the shadow column, and frames with sprites, a
masked middle texture, the weapon and shadows.
`test_p4` spawns the things of E1M1, checks their links into sectors and
blockmap, and walks player 1 by a fixed list of ticcmds: speed, steps, the
stop at a wall, coming to rest, and the place he ends up at.

Looking at the data (title picture, wall textures, flats, sprites, the lines
of E1M1); with a folder argument each page is saved as a PNG:

```
build/jdBasic.exe jdb/doom2/view_p1.jdb [folder]
```

Flying a camera through a level, through walls and all, the things standing
where they were spawned. Arrow keys turn and move, A and D step sideways, Shift runs,
PageUp and PageDown change the view size, Escape quits:

```
build/jdBasic.exe -c jdb/doom2/view_p2.jdb
jdb/doom2/view_p2.exe [episode] [map] [folder for screenshots]
```

Playing a level with the game's own controls: collision, steps, sliding
along walls, the bobbing view. Monsters stand still and doors stay shut:

```
build/jdBasic.exe -c jdb/doom2/view_p4.jdb
jdb/doom2/view_p4.exe [episode] [map]
```

## Tools

```
build/jdBasic.exe jdb/doom2/tools/gen_data.jdb     regenerate tables.jdb and info.jdb
build/jdBasic.exe jdb/doom2/tools/ref_p1.jdb       print the reference numbers of test_p1
build/jdBasic.exe jdb/doom2/walk.jdb "f70 l32 F80"  walk player 1 by a script and print where he is
```
