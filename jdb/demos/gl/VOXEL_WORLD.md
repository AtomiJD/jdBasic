# Voxel World (jdBasic GL 3.3)

Minecraft-style voxel demo built as a Fingerübung on 2026-11-18. First GL demo
to drive editing + a character on top of the `GL.*` wrapper.

## Files

| File | Purpose |
|------|---------|
| `voxel_world.jdb` | The actual demo. 16×8×16 chunk, FPS controls, dig/place, F5 third-person girl with pink dress. |
| `voxel_smoke.jdb` | Headless mesh-build test. Asserts top-face count equals NX×NZ (no window needed). |
| `voxel_capture.jdb` | Renders one frame + screenshot to `voxel_capture.png`. Our green/red smoke. |
| `voxel_phase2_test.jdb` | Headlessly drives one dig + one place from a fixed camera. Proves raycast + rebuild. Writes `voxel_phase2.png`. |
| `voxel_girl_capture.jdb` | Self-contained render of the Phase 3 character from a 3/4 front view. Writes `voxel_girl.png`. |

## What works

**Phase 1 (base world + FPS cam)**
- Vectorised height map via `IOTA` + `SIN`/`COS` (`heights = FLOOR(hd + 2.5)`)
- Scalar fill loop paints grass (top), dirt (h-2..h-1), stone (rest) into a flat `world[x + z*NX + y*NX*NZ]` array
- `build_mesh` SUB iterates cells; for each non-air cell it emits a face per direction whose neighbour is air → ~1028 faces instead of 9108
- Procedural fragment shader: per-type base colour, `hash12` world-space noise, Lambert light, top/bottom brightness bias, edge darkening - no PNG assets needed
- WASD + Space/LShift + arrows + ESC via `ON "KEYDOWN"`/`"KEYUP"` into a `keys` map polled with `is_down(k$)`

**Phase 2 (edit the world)**
- `cast_ray` SUB (Amanatides-Woo) walks the voxel grid from eye along view dir up to `REACH=32`, writes hit cell + entry face into globals
- `OnMouseDown`: btn 1 digs the hit cell, btn 3 places `sel_type` on the adjacent air cell; `build_mesh` + `rebuild_gl_mesh` (delete+recreate VBO/EBO, re-run `GL.ATTRIB`s against the live VAO) refresh the geometry
- Keys 1/2/3 cycle `sel_type` (grass/dirt/stone)
- NDC crosshair in its own shader so clicks line up with the pixel centre
- `CULL_FACE` on - face winding fixed CCW-from-outside for all six faces (+X, -X, +Z needed inverting)

**Phase 3 (arm + third-person girl)**
- FS gains `uniform float uForceType` - if ≥ 0 overrides per-vertex type, so one shader does world (`-1`) and every character part (`4..6`)
- Extended palette: 4 skin, 5 pink dress, 6 brown hair (noise reduced to 0.06 for character types)
- First person: skin arm cube behind the held block, both bob along `bob_phase` while any WASD is held
- F5 toggles `tpv`. Third-person render camera offsets `TPV_DIST=4` units behind `cam_x/y/z` with 0.4 lift; raycast still starts from the eye so the crosshair stays truthful
- `draw_character` emits 10 cubes (head + flat hair cap + two side bob strips + pink dress top + wider pink skirt + 2 arms + 2 legs + held block in right hand), anchored at `(cam_x, cam_y - EYE_OFFSET, cam_z)`, rotated by `-yaw` around Y so she faces the view direction. Arms + legs counter-swing on `bob_phase`.

## Build + run

```bash
# One-time: build the Linux binary with GL
GFX=1 IMGUI=1 NATIVEC=1 OPENGL=1 ./build.sh

# Verify (should list GL3.3 in Features)
./build/jdbasic --version

# Run
./build/jdbasic jdb/demos/gl/voxel_world.jdb

# Smoke-test (no window)
./build/jdbasic jdb/demos/gl/voxel_smoke.jdb

# Screenshot verifications (need an X display)
./build/jdbasic jdb/demos/gl/voxel_capture.jdb        # → voxel_capture.png
./build/jdbasic jdb/demos/gl/voxel_phase2_test.jdb    # → voxel_phase2.png
./build/jdbasic jdb/demos/gl/voxel_girl_capture.jdb   # → voxel_girl.png
```

## Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Horizontal movement relative to yaw |
| Space / Left Shift | Up / down |
| ← / → | Yaw |
| ↑ / ↓ | Pitch |
| 1 / 2 / 3 | Select grass / dirt / stone |
| LMB | Dig the cell the crosshair hits |
| RMB | Place `sel_type` on the face the crosshair hits |
| F5 | Toggle third-person view (the girl) |
| ESC | Quit |

## Architecture notes

- Vertex layout for world and character parts is identical: `pos(3) + normal(3) + type(1)` = 7 floats, 28-byte stride. Lets one shader + one VBO topology serve everything.
- Character parts all reuse the `held_vao` / `held_vbo` (a unit cube in 0..1). The model matrix does the heavy lifting: `translate(player) * rotateY(-yaw) * translate(local) * scale * translate(-0.5)` recentres the cube at its local origin before final placement.
- Depth test is off for the first-person hand and the crosshair; `CULL_FACE` + CCW-outside winding alone are enough to keep the held block from showing its backfaces.
- `cam_x/y/z` is the "eye" position. It drives raycasts and movement. The render camera is derived - identical in fpv, offset backwards in tpv. Clicks always fire from the eye so the crosshair stays truthful in both modes.

## Known limits / things we deferred

- World is a single 16×8×16 chunk. No neighbours, no infinite terrain.
- No collision - WASD walks through solid cells. Fine for a flight-cam demo, awkward for a game.
- Mesh rebuild is O(NX·NY·NZ) via scalar loops; cheap for one chunk but would need per-chunk dirty-flag + a face-pool for multi-chunk.
- No day/night, no shadows, no ambient occlusion - one hardcoded light direction in the FS.
- Linux `libjdbrt.so` still ships without OpenGL, so a `-c`-compiled voxel program won't link GL symbols. The interpreter binary has GL3.3.
- Mouse-look uses arrow keys because SDL relative-mouse-mode isn't exposed through the GL/GFX namespaces yet.
- Character rig is static poses + a sinusoidal arm/leg swing - no per-limb articulation beyond that, no click/swing animation.

## Ideas parked for later

- **Phase 4 polish**: swing animation on LMB (arm lunges forward), place animation on RMB
- **Phase 5 world**: Perlin noise instead of sinus; 3×3 chunk grid; chunk-level dirty mesh rebuilds
- **Character**: pony-tail cube hanging off the back; palette cycling with `C`; expression (eye/mouth) drawn in the shader per-face
- **Camera**: mouse-look once SDL relative mode is wired in; mouse-wheel zoom in tpv
- **Physics**: gravity + collision so you walk on top of the world instead of flying; jump on Space
- **AO / shadow**: simple baked AO from `6 - visible_face_count` per vertex; directional shadow via a depth pre-pass into a small texture
