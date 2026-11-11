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
  not in the repository), in `jdb/doom/wad/classic/`. The numbers in the
  tests `test_p1` to `test_p9` belong to this file.
- For DOOM II a `doom2.wad`. `test_doom2` reads `jdb/doom/wad/doom2.wad`
  or the file the environment variable `DOOM2WAD` names; its numbers
  belong to the re-release IWAD of 14,951,361 bytes. That file has the
  demos of version 1.9 and full-screen pictures 560 pixels wide;
  `V_DrawPatch` shows the middle 320 columns of such a patch.
- The game looks for its WAD as `D_MAIN.IdentifyVersion$` says: the file
  named with `-iwad`, else the first of `doom2.wad`, `doomu.wad`,
  `doom.wad`, `doom1.wad` in the folder `DOOMWADDIR` names; without that
  variable in the working directory, then in `jdb/doom/wad/classic/`.
- `jdb/doom/linuxdoom-1.10/` (the C source, not in the repository) only to run
  the generator again.

## Modules

| Module | C file | Content |
|---|---|---|
| `d_main.jdb` | d_main.c | where the IWAD is found, the event queue, `D_Display` with the wipe, the title loop of pages and demos, `D_DoomMain` |
| `doom.jdb` | d_main.c, i_video.c | the program: keyboard and mouse events, the main loop |
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
| `g_game.jdb` | g_game.c | `G_BuildTiccmd`, `G_InitNew`, `G_DoLoadLevel`, `G_PlayerReborn`, `G_Ticker`, demo playback (`G_DeferedPlayDemo`), `G_ExitLevel` and `G_SecretExitLevel` (they set `gameaction`) |
| `p_tick.jdb` | p_tick.c | thinkers and the functions they run by number (`TF_`), `P_Ticker` |
| `p_mobj.jdb` | p_mobj.c, p_mobj.h | things: spawn, remove, states, movement in x, y and z, missiles |
| `p_maputl.jdb` | p_maputl.c | sides and distances, line openings, thing links, blockmap iterators, `P_PathTraverse` |
| `p_map.jdb` | p_map.c | `P_CheckPosition`, `P_TryMove`, `P_SlideMove`, `P_UseLines`, `P_ChangeSector`, `P_AimLineAttack`, `P_LineAttack`, `P_RadiusAttack` |
| `p_user.jdb` | p_user.c | `P_PlayerThink`, movement by ticcmd, view height and bob |
| `p_spec.jdb` | p_spec.c | animated flats and textures, the sector utilities, `P_CrossSpecialLine`, `P_ShootSpecialLine`, `P_PlayerInSpecialSector`, `P_UpdateSpecials`, `EV_DoDonut`, `P_SpawnSpecials` |
| `p_doors.jdb` | p_doors.c | doors: `T_VerticalDoor`, `EV_DoDoor`, `EV_DoLockedDoor`, `EV_VerticalDoor` |
| `p_plats.jdb` | p_plats.c | platforms: `T_PlatRaise`, `EV_DoPlat`, `EV_StopPlat` |
| `p_floor.jdb` | p_floor.c | `T_MovePlane`, `T_MoveFloor`, `EV_DoFloor`, `EV_BuildStairs` |
| `p_ceilng.jdb` | p_ceilng.c | ceilings and crushers: `T_MoveCeiling`, `EV_DoCeiling`, `EV_CeilingCrushStop` |
| `p_lights.jdb` | p_lights.c | fire flicker, light flash, strobe, glow, lights switched by lines |
| `p_switch.jdb` | p_switch.c | the switch textures, buttons, `P_UseSpecialLine` |
| `p_telept.jdb` | p_telept.c | `EV_Teleport` |
| `p_sight.jdb` | p_sight.c | `P_CheckSight`: the REJECT table and a walk through the BSP tree |
| `p_inter.jdb` | p_inter.c | pickups, `P_DamageMobj`, death and what a monster drops |
| `p_enemy.jdb` | p_enemy.c | `P_NoiseAlert`, looking, chasing, the attacks of the monsters of DOOM and DOOM II, `A_Call` (the action of a state by its number) |
| `p_pspr.jdb` | p_pspr.c | the weapon sprites and the weapon actions |
| `d_items.jdb` | d_items.c | the weapons: ammunition and sprite states |
| `st_stuff.jdb` | st_stuff.c, st_lib.c | the status bar, the face, the palette flashes, the cheats |
| `hu_stuff.jdb` | hu_stuff.c, hu_lib.c | the message line, the level name on the automap, the font |
| `am_map.jdb` | am_map.c | the automap |
| `wi_stuff.jdb` | wi_stuff.c | the intermission of a single player, of DOOM and of DOOM II |
| `f_finale.jdb` | f_finale.c | the text and the picture at the end of an episode; the texts between the levels of DOOM II and its cast |
| `f_wipe.jdb` | f_wipe.c | the melting screen |
| `m_menu.jdb` | m_menu.c | the menus with load and save, the function keys, the mouse in the menu |
| `m_misc.jdb` | m_misc.c | the settings in `default.cfg` |
| `p_saveg.jdb` | p_saveg.c | what a savegame holds: players, world, things, moving sectors and lights |
| `s_sound.jdb` | s_sound.c | the eight channels: volume by distance, stereo by the side, priorities; which music plays |
| `i_sound.jdb` | i_sound.c | the mixer: 8 bit samples of the sound lumps at 11025 Hz into a stereo queue |
| `i_music.jdb` | (DMX) | plays the MUS scores of the WAD on the synthesizer of the language |
| `sounds.jdb` | sounds.c | generated: the names, priorities and links of the sounds, the names of the music |

Not ported in files that are otherwise there:

- Recording a demo.
- The chat, the intermission and the automap arrows of net games.
- The quit messages: the menu always asks the plain question.
- Net games: a second player in a demo, coming back at a start spot,
  deathmatch.
- The joystick, the chat macros and the number of sound channels among
  the settings.

Where the port leaves the original on purpose:

- A mover that C allocates without clearing (a stair step, a floor that
  finds no neighbour to take its flat from) starts with its unset fields
  at zero.
- `EV_VerticalDoor` on a sector that is busy as a platform or a floor does
  nothing; C would write into that structure as if it were a door.
- The switch list takes the registered set when the WAD has its textures,
  as DOOM 1.9 does for The Ultimate Doom; the released source asks for
  `gamemode == registered` only.
- The status bar is drawn whole every frame; the original redraws only
  the parts whose value changed, over a background kept in a screen of
  its own. The pixels on the screen are the same.
- A cheat is found by comparing the last keys typed with its word; the
  original walks a state machine over scrambled key codes (m_cheat.c).
- The wipe reads the columns of the two pictures where they are instead
  of turning the pictures on their side first.
- A savegame has the sections of the original in their order and leaves
  out what the original leaves out (who a thing is after, sound targets,
  fire lights, platforms in stasis, waiting buttons, the random index;
  heights as whole numbers), but its records hold the fields of this
  port, 32 bits each. A `.dsg` of DOOM.EXE does not load and is told
  apart by its version text. The missile of a revenant forgets whom it
  follows; the original keeps a pointer that no longer points anywhere.
- `idclev` works in DOOM II (two digits, 01 to 32); the released source
  sets the episode to 0 there and then refuses it.
- Where the original would follow a NULL pointer the action does
  nothing: a mancubus without a target, the fire of an arch-vile that
  is gone, the brain of a level without target spots.
- The mouse pointer is not held in the window; the X11 code of the
  original grabs it. Turning stops where the pointer leaves the window
  or meets the edge of the screen (trakr jdbasic #986).
- The settings are in `default.cfg` in the working directory, as in the
  DOS release; the Linux release keeps them in `~/.doomrc`.
- Music is not General MIDI: each MIDI channel is a track of the
  synthesizer of the language with a waveform and an envelope picked by
  the family of the instrument, the percussion its built-in drum kit.
  The events of a score are played once a tic, four 140ths of a second
  at a time.
- The mixer follows i_sound.c of the Linux release, with three changes:
  a sound that plays follows its source in volume and side (the Linux
  release leaves that out, DOS did it); the rate a sound was recorded
  at is honoured; the volume of 0 to 15 is taken times 8 into the
  volume table of 128 steps. A sound is over when its time is, whether
  a device plays it or not.

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
build/jdBasic.exe jdb/doom2/test_p5.jdb
build/jdBasic.exe -c jdb/doom2/test_p5.jdb && jdb/doom2/test_p5.exe
build/jdBasic.exe jdb/doom2/test_p6.jdb
build/jdBasic.exe -c jdb/doom2/test_p6.jdb && jdb/doom2/test_p6.exe
build/jdBasic.exe jdb/doom2/test_p7.jdb
build/jdBasic.exe -c jdb/doom2/test_p7.jdb && jdb/doom2/test_p7.exe
build/jdBasic.exe jdb/doom2/test_p8.jdb
build/jdBasic.exe -c jdb/doom2/test_p8.jdb && jdb/doom2/test_p8.exe
build/jdBasic.exe jdb/doom2/test_p9.jdb
build/jdBasic.exe -c jdb/doom2/test_p9.jdb && jdb/doom2/test_p9.exe
DOOMWADDIR=jdb/doom/wad build/jdBasic.exe jdb/doom2/test_p9.jdb
build/jdBasic.exe jdb/doom2/test_doom2.jdb
build/jdBasic.exe -c jdb/doom2/test_doom2.jdb && jdb/doom2/test_doom2.exe
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
`test_p5` runs the specials of E1M1: a door opened by hand and turned
while it closes, a button, a lift, floors that lower, a fast door, the
scrolling wall, the animated flats, a glowing light, a teleport, the secret
and the exit switch. The tics each mover takes are worked out by hand from
the heights in the map. Then every special line of E1M1 to E1M3 is
triggered and the state of the sectors compared with what was recorded.
`test_p6` checks ammunition, armor, damage, pickups, sight through a door,
a barrel and a trooper against the rules of p_inter.c, and plays DEMO1,
DEMO2 and DEMO3 to their end mark (20 seconds interpreted). In each the
player dies two to seven seconds before the recording stops, as a
recording of a death does; where he lies, his kills and the random index
are compared with what was recorded. The demos were not compared with a
run of the original program.
`test_p7` runs the shell without a window: the wipe, the menu by its
keys, the cheats, the status bar and the automap against hashes of
pictures that were looked at, the way over the intermission to the next
and the secret level, the finale, and one round of the title loop with
its four demos (25 seconds interpreted).
`test_p8` checks sound and music without a device: the samples the mixer
makes against the bytes of the lump and volumes worked out by hand, volume
and side by the place of the source, the channels, and every score of the
music read to its end mark. What it sounds like is for the ear:
`soundtest.exe` plays every sound and a piece of music.
`test_p9` saves a game in the middle of four levels with every special
line set off and monsters awake, plays on, loads, and compares things,
world, movers and player with what they were; then it saves and loads
through the menu and the quick keys, turns, walks and fires with the
mouse (the angles worked out by hand), moves the menu with it, and
writes and reads the settings file. With `DOOMWADDIR` set to a folder
that holds `doom2.wad` it does the same on levels of DOOM II. Its
savegames go to `tmp/doom_p9`.
`test_doom2` plays the three demos of DOOM II from tic 0, as -playdemo
does (the missile of a revenant turns every fourth tic of the game, not
of the level): in each the player dies one to four seconds before the
recording stops. Like those of DOOM they were not compared with a run of
the original program. It walks the level order with both secret levels
and the texts, puts each monster of DOOM II to work on a level, raises a
corpse, kills what MAP07, MAP30 and MAP32 hang their specials on, goes
through the cast, fires the super shotgun and runs one round of the
title loop (30 seconds interpreted).

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

The game: the title loop with its demos, the menu, the levels with status
bar, messages and automap, intermission and finale, savegames. Any key
opens the menu. With an episode and a map it starts there; the levels of
DOOM II are in episode 1. Arrow keys or W and S move, A and D or comma
and full stop step sideways, Shift runs, Ctrl fires, Space uses, 1 to 7
pick a weapon, Tab shows the map, Escape the menu, F2 and F3 save and
load, F6 and F9 do so quickly. The mouse turns, walks and fires;
`use_mouse 0` in `default.cfg` turns it off:

```
build/jdBasic.exe -c jdb/doom2/doom.jdb
jdb/doom2/doom.exe [-iwad file] [episode map [skill]]
```

A folder to hand on, with the program, its libraries and a note; the WAD
is copied beside `doom.exe` by whoever plays:

```
build/jdBasic.exe jdb/doom2/tools/pack.jdb [folder]
```

## Tools

```
build/jdBasic.exe jdb/doom2/tools/gen_data.jdb     regenerate tables.jdb and info.jdb
build/jdBasic.exe jdb/doom2/tools/gen_sounds.jdb   regenerate sounds.jdb
build/jdBasic.exe jdb/doom2/tools/gen_texts.jdb    write the texts of d_englsh.h into f_finale.jdb and hu_stuff.jdb
build/jdBasic.exe jdb/doom2/tools/ref_p1.jdb       print the reference numbers of test_p1
build/jdBasic.exe jdb/doom2/walk.jdb "f70 l32 F80"  walk player 1 by a script and print where he is
build/jdBasic.exe -c jdb/doom2/sweep.jdb           then sweep.exe: trigger every special line of every level
build/jdBasic.exe -c jdb/doom2/playdemo.jdb        then playdemo.exe DEMO1 [show | folder]: play a demo of the WAD IdentifyVersion$ finds
build/jdBasic.exe -c jdb/doom2/soundtest.jdb       then soundtest.exe [seconds] [music] [volume]: play every sound, then music
```
