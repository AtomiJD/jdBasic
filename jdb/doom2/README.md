# DOOM in jdBasic

A file-by-file port of linuxdoom-1.10 to jdBasic, compiled with `-c`. The play
simulation is deterministic, so the demos inside the WAD serve as its test: a
demo that runs in sync to its end proves the game logic.

Tracker: jdTrakr project `doom` (epic #967, one ticket per phase).

## Rules of the port

- Same algorithms as the C source, one module per C file, same function names.
- `fixed_t` is 16.16 fixed point in an `INTEGER`, kept in the signed 32-bit
  range. `angle_t` is a 32-bit binary angle, masked with `M_FIXED.ANGLE_MASK`
  after every addition and subtraction.
- The frame path reads flat `INTEGER` arrays only: no strings, no map lookups.
  Names (textures, flats, sprites) become numbers when a level loads.
- The framebuffer holds palette indices; the palette is applied when the frame
  is shown.
- `tables.jdb` and `info.jdb` are generated from the C source by
  `tools/gen_data.jdb` and are never edited by hand.
- A cross-module `FUNC` call costs about 20 ns compiled, the same arithmetic
  inline about 5 ns. Inner loops over pixels write `FixedMul` out; everything
  else calls it.

## What is needed

- `jdb/doom/wad/doom.wad` (not in the repository).
- `jdb/doom/linuxdoom-1.10/` (the C source, not in the repository) only to run
  the generator again.

## Modules

| Module | C file | Content |
|---|---|---|
| `m_fixed.jdb` | m_fixed.c, tables.h | `FixedMul`, `FixedDiv`, `ToS32`, angle constants |
| `m_random.jdb` | m_random.c | `P_Random`, `M_Random` |
| `tables.jdb` | tables.c | `finetangent`, `finesine`, `tantoangle` (generated) |
| `info.jdb` | info.c, info.h, sounds.h | states, thing types, sprite names, enums (generated) |
| `w_wad.jdb` | w_wad.c | WAD directory and lump access |

## Running the checks

From the repository root:

```
build/jdBasic.exe jdb/doom2/test_p0.jdb
build/jdBasic.exe -c jdb/doom2/test_p0.jdb && jdb/doom2/test_p0.exe
```

Regenerating the data modules:

```
build/jdBasic.exe jdb/doom2/tools/gen_data.jdb
```
