# DOOM in jdBasic

A file-by-file port of linuxdoom-1.10 to jdBasic, compiled with `-c`. The play
simulation is deterministic, so the demos inside the WAD serve as its test: a
demo that runs in sync to its end proves the game logic.

Tracker: jdTrakr project `doom` (epic #967, one ticket per phase).

## Rules of the port

- Same algorithms as the C source, one module per C file, same function names.
  Modules import each other in circles where the C files call each other.
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

- `jdb/doom/wad/doom.wad` (not in the repository). The numbers in the tests
  belong to the re-release IWAD (12,996,515 bytes), whose E1M1 has 143 things
  and whose full-screen pictures are 560 pixels wide; `V_DrawPatch` shows the
  middle 320 columns of such a patch.
- `jdb/doom/linuxdoom-1.10/` (the C source, not in the repository) only to run
  the generator again.

## Modules

| Module | C file | Content |
|---|---|---|
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
| `r_segs.jdb` | r_segs.c | `R_StoreWallRange`, `R_RenderSegLoop` |
| `r_plane.jdb` | r_plane.c | visplanes, spans, sky, the clip arrays |
| `r_draw.jdb` | r_draw.c | `R_DrawColumn`, `R_DrawSpan`, the view window |
| `r_sky.jdb` | r_sky.c | sky flat and texture |
| `r_things.jdb` | r_things.c | weapon sprite scales so far |

## Running the checks

From the repository root, each test interpreted and compiled:

```
build/jdBasic.exe jdb/doom2/test_p0.jdb
build/jdBasic.exe -c jdb/doom2/test_p0.jdb && jdb/doom2/test_p0.exe
build/jdBasic.exe jdb/doom2/test_p1.jdb
build/jdBasic.exe -c jdb/doom2/test_p1.jdb && jdb/doom2/test_p1.exe
build/jdBasic.exe jdb/doom2/test_p2.jdb
build/jdBasic.exe -c jdb/doom2/test_p2.jdb && jdb/doom2/test_p2.exe
```

`test_p0` covers fixed point, random numbers and the generated tables.
`test_p1` loads E1M1 and compares what the modules hold with numbers
`tools/ref_p1.jdb` computed from the WAD file on a path of its own.
`test_p2` checks the view tables and the angle arithmetic and renders frames
of E1M1: each has to cover every pixel of the view and to match the hash
recorded for it, interpreted and compiled alike.

Looking at the data (title picture, wall textures, flats, sprites, the lines
of E1M1); with a folder argument each page is saved as a PNG:

```
build/jdBasic.exe jdb/doom2/view_p1.jdb [folder]
```

Walking the view through a level (walls, floors, ceilings, sky; no things,
no collision). Arrow keys turn and move, A and D step sideways, Shift runs,
PageUp and PageDown change the view size, Escape quits:

```
build/jdBasic.exe -c jdb/doom2/view_p2.jdb
jdb/doom2/view_p2.exe [level] [folder for screenshots]
```

## Tools

```
build/jdBasic.exe jdb/doom2/tools/gen_data.jdb     regenerate tables.jdb and info.jdb
build/jdBasic.exe jdb/doom2/tools/ref_p1.jdb       print the reference numbers of test_p1
```
