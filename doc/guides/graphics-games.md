# Graphics and games

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

This guide is for drawing, animating and writing a small game. It opens a
window, draws shapes, builds a game loop with keyboard and mouse input, and then
puts the pieces together into a complete paddle game. After that come sprites,
tile maps, sound and compiling the game to an `.exe`.

Every program here needs a build with the `GFX` flag (the default Windows
build has it). Save a program as a `.jdb` file and run it with
`jdBasic.exe file.jdb`. The full list of drawing and window functions is in
[Graphics and Multimedia Functions](../languages.md#graphics-and-multimedia-functions).

## 1. Open a window and draw

`SCREEN width, height, title$` opens a window. The drawing statements take
coordinates in pixels, with 0, 0 in the top left corner, and an optional
colour as three values from 0 to 255.

```basic
SCREEN 640, 480, "First drawing"

CLS 20, 24, 40
PSET 20, 20, 255, 255, 255
LINE 20, 460, 620, 300, 255, 200, 0
RECT 60, 80, 160, 100, TRUE, 40, 160, 255
RECT 60, 80, 160, 100, FALSE, 255, 255, 255
CIRCLE 420, 160, 70, TRUE, 230, 60, 80
DRAWCOLOR 120, 255, 120
CIRCLE 420, 160, 90
TEXT 60, 220, "Hello, graphics", 255, 255, 255
SCREENFLIP

SLEEP 3000
```

What each line does:

* `CLS r, g, b` fills the whole window with one colour. Without a window,
  `CLS` clears the console.
* `RECT` and `CIRCLE` take a `fill` flag before the colour: `TRUE` draws a
  filled shape, `FALSE` only the outline.
* `DRAWCOLOR r, g, b` sets the colour for drawing calls that leave out their
  own colour, like the second `CIRCLE`.
* `TEXT x, y, text$` writes with the built-in font. `SETFONT file$, size`
  switches to a TrueType font, for example
  `SETFONT "C:/Windows/Fonts/consola.ttf", 16`.

Other shapes are `ELLIPSE`, `ROUNDED_RECT` and `CIRCLE_SECTOR`, and
`GFX.LOADIMAGE` with `GFX.DRAWIMAGE` puts a PNG or JPG on the screen.

### Why SCREENFLIP

Drawing goes to a hidden back buffer. Nothing appears in the window until
`SCREENFLIP` shows that buffer. After the flip the back buffer starts empty
again, so a program that animates redraws the whole picture every frame:
clear, draw everything, flip. This is what keeps the picture from flickering.

`SCREENFLIP` also collects the window's input events (keys, mouse, the close
button). A loop that never flips or sleeps does not see any input, and the
window stops responding.

## 2. The game loop

A game is one loop that runs about 60 times per second. Each pass reads the
input, updates the game state and draws one frame.

```basic
SCREEN 640, 480, "Game loop"

DIM running AS INTEGER = 1
DIM px AS DOUBLE = 320
DIM py AS DOUBLE = 240
DIM speed AS DOUBLE = 200
DIM lastKey$ AS STRING = ""

SUB OnQuit(info)
    running = 0
ENDSUB
SUB OnKey(info)
    lastKey$ = info[0]{"key"}
    IF lastKey$ = "Escape" THEN running = 0
ENDSUB
ON "QUIT" CALL OnQuit
ON "KEYDOWN" CALL OnKey

DIM lastT AS DOUBLE = TICK()
DIM now AS DOUBLE = 0
DIM dt AS DOUBLE = 0
DO WHILE running = 1
    now = TICK()
    dt = (now - lastT) / 1000.0
    lastT = now

    ' held keys move the dot by speed * elapsed time
    IF GFX.KEYSTATE("Left") THEN px = px - speed * dt
    IF GFX.KEYSTATE("Right") THEN px = px + speed * dt
    IF GFX.KEYSTATE("Up") THEN py = py - speed * dt
    IF GFX.KEYSTATE("Down") THEN py = py + speed * dt
    ' the left mouse button pulls the dot to the pointer
    IF MOUSEB(1) THEN
        px = MOUSEX()
        py = MOUSEY()
    ENDIF

    CLS 10, 10, 30
    CIRCLE px, py, 12, TRUE, 255, 220, 0
    TEXT 10, 10, "Arrows or mouse to move, Esc to quit", 200, 200, 200
    TEXT 10, 34, "Last key: " + lastKey$, 160, 160, 160
    SCREENFLIP
    SLEEP 16
LOOP
GFX.CLOSE
PRINT "Bye"
```

### Frame timing

`TICK()` returns the milliseconds since the program started. The loop measures
the time since the last frame in seconds (`dt`) and multiplies every movement
by it, so the dot moves 200 pixels per second whether a frame takes 16 ms or
30 ms. `SLEEP 16` hands the rest of the frame back to the system and keeps the
loop near 60 frames per second. On Windows `SLEEP` has a granularity of about
15.6 ms, which is one more reason to move things by `dt` and not by a fixed
amount per frame.

### Keyboard

There are three ways to read keys, and games usually mix two of them:

* `GFX.KEYSTATE(name$)` is `TRUE` while a key is held down. Use it for
  movement. The names are SDL key names: `"Left"`, `"Right"`, `"Up"`,
  `"Down"`, `"Space"`, `"Return"`, `"Escape"`, `"A"` to `"Z"`,
  `"Left Shift"`, `"F6"`.
* `ON "KEYDOWN" CALL handler` runs a `SUB` once per key press. The `SUB`
  takes one parameter; `info[0]` is a map with `"key"` (the key name),
  `"scancode"`, `"keycode"` and `"repeat"` (`TRUE` for auto-repeat). Use it
  for single actions such as pause or menu keys. `ON "KEYUP"` works the same
  way.
* `INKEY$()` returns the last key pressed, or `""`. In a graphics window it
  gives the key name for most keys (`"A"`, `"Right"`), and `CHR$(27)` for
  Escape and `CHR$(13)` for Enter. It keeps only one key between calls, so
  it suits menus better than action games.

### Mouse and gamepad

`MOUSEX()` and `MOUSEY()` give the pointer position in the window, and
`MOUSEB(n)` is `TRUE` while button `n` is held (1 left, 2 middle, 3 right).
`GFX.MOUSEX()`, `GFX.MOUSEY()` and `GFX.MOUSEBUTTON(n)` read the same
values. A quick click can fall between two frames;
`ON "MOUSEDOWN"`, `ON "MOUSEUP"` and `ON "MOUSEMOVE"` deliver every event
with `info[0]{"x"}`, `info[0]{"y"}` and, for the buttons, `info[0]{"button"}`.

Gamepads are read with `JOY.COUNT()`, `JOY.AXIS`, `JOY.BUTTON` and
`JOY.HAT`; see [Mouse / Joystick / Gamepad Input](../languages.md#mouse--joystick--gamepad-input).

### Quitting cleanly

`ON "QUIT"` fires when the user closes the window. The handler only sets a
flag, and the loop ends at the next check. `GFX.CLOSE` then closes the window,
and the program can go on in the console.

## 3. A complete game: Paddle

This game uses only what the first two steps showed. A ball bounces off the
walls; you keep it in play with a paddle. Each hit scores a point and makes
the ball a little faster, and where the ball lands on the paddle changes its
angle.

```basic
' Paddle: keep the ball in play. Left/Right move, Space restarts, Esc quits.
CONST W = 640
CONST H = 480
CONST PADW = 90
CONST R = 8

SCREEN W, H, "Paddle"

DIM running AS INTEGER = 1
DIM padX AS DOUBLE = (W - PADW) / 2
DIM bx AS DOUBLE = 0
DIM by AS DOUBLE = 0
DIM vx AS DOUBLE = 0
DIM vy AS DOUBLE = 0
DIM score AS INTEGER = 0
DIM alive AS INTEGER = 1

SUB OnQuit(info)
    running = 0
ENDSUB
ON "QUIT" CALL OnQuit

SUB NewBall()
    bx = W / 2
    by = 60
    vx = 180
    IF RND(1) < 0.5 THEN vx = -180
    vy = 220
    score = 0
    alive = 1
ENDSUB

NewBall()
DIM lastT AS DOUBLE = TICK()
DIM now AS DOUBLE = 0
DIM dt AS DOUBLE = 0

DO WHILE running = 1
    now = TICK()
    dt = CLAMP(now - lastT, 0, 50) / 1000.0
    lastT = now

    IF GFX.KEYSTATE("Escape") THEN running = 0
    IF GFX.KEYSTATE("Left") THEN padX = padX - 400 * dt
    IF GFX.KEYSTATE("Right") THEN padX = padX + 400 * dt
    padX = CLAMP(padX, 0, W - PADW)

    IF alive = 1 THEN
        bx = CLAMP(bx + vx * dt, R, W - R)
        by = by + vy * dt
        IF bx <= R OR bx >= W - R THEN vx = -vx
        IF by < R THEN vy = ABS(vy)
        ' paddle hit: bounce up, faster, angled by where the ball landed
        IF vy > 0 AND by > H - 40 - R AND by < H - 30 AND bx > padX AND bx < padX + PADW THEN
            vy = -vy * 1.05
            vx = vx + (bx - (padX + PADW / 2)) * 3
            score = score + 1
        ENDIF
        IF by > H + R THEN alive = 0
    ELSEIF GFX.KEYSTATE("Space") THEN
        NewBall()
    ENDIF

    CLS 15, 15, 35
    RECT padX, H - 40, PADW, 10, TRUE, 80, 200, 255
    CIRCLE bx, by, R, TRUE, 255, 220, 0
    TEXT 10, 10, "Score: " + STR$(score), 255, 255, 255
    IF alive = 0 THEN TEXT W / 2 - 150, H / 2, "Game over. Space to restart", 255, 120, 120
    SCREENFLIP
    SLEEP 16
LOOP
```

A few details worth copying into your own games:

* `CLAMP(now - lastT, 0, 50)` caps `dt` at 50 ms. When the window is dragged
  or the machine stalls, the ball would otherwise jump through the paddle on
  the next frame.
* The paddle test checks `vy > 0` first, so a ball that is already on its way
  up cannot be hit twice.
* The game state lives in a few variables, and `NewBall()` resets all of them.
  A restart is one call.
* Every variable is declared with `DIM` and a type. The interpreter does not
  need that, but the native compiler does (section 6).

Ideas for extending it: bricks stored in an array, a second ball, a high
score kept in a file.

## 4. Sprites and tile maps

Shapes are enough for Paddle. Bigger games use images: sprites for things
that move, and a tile map for the level.

A sprite is an image handle with a position, scale, rotation and animation.
`SPRITE.LOAD("hero.png")` loads one from a file; `SPRITE.CREATE(w, h)` makes
an empty one that you fill with `SPRITE.SETBUFFER` (a flat array of RGBA
values, four per pixel). `SPRITE.DRAW_ALL` draws every visible sprite in
z-order.

A tile map is a 2D array of tile numbers drawn from a tileset image.
`TILEMAP.CREATE name$, image, data, tile_w, tile_h` builds one; tile 1 is the
first tile in the image, and 0 is an empty cell. `TILEMAP.COLLIDES(sprite,
name$)` is `TRUE` when the sprite overlaps any non-empty tile of that map, so
walls usually go into a map of their own.

The camera (`CAM.*`) shifts everything that `SPRITE.DRAW_ALL` and
`TILEMAP.DRAW` draw. `CAM.FOLLOW sprite` keeps a sprite in the middle of the
screen, and `CAM.BOUNDS` stops the view at the edges of the world.
`SPRITE.UPDATE` moves the camera, advances animations and applies sprite
velocities; call it once per frame.

This program makes its own tileset and hero, so it runs without any image
files. It writes `tiles.png` into the current folder.

```basic
CONST T = 16
CONST COLS = 80
CONST ROWS = 60
SCREEN 640, 480, "Sprites and tiles"

' a tileset image: two 16x16 tiles stacked, tile 1 grass, tile 2 wall
DIM grass = RESHAPE([40, 130, 60, 255], [T * T * 4])
DIM stone = RESHAPE([120, 110, 100, 255], [T * T * 4])
DIM sheet = SPRITE.CREATE(T, T * 2)
SPRITE.SETBUFFER sheet, APPEND(grass, stone)
SPRITE.SAVE sheet, "tiles.png"
SPRITE.DELETE sheet
DIM tileset = GFX.LOADIMAGE("tiles.png")

' two maps: grass everywhere, walls on the border and scattered at random
DIM ground = RESHAPE([1], [ROWS, COLS])
DIM walls = RESHAPE([0], [ROWS, COLS])
FOR r = 0 TO ROWS - 1
    FOR c = 0 TO COLS - 1
        IF r = 0 OR c = 0 OR r = ROWS - 1 OR c = COLS - 1 OR RND(1) < 0.1 THEN walls[r, c] = 2
    NEXT c
NEXT r
walls[2, 2] = 0
TILEMAP.CREATE "ground", tileset, ground, T, T
TILEMAP.CREATE "walls", tileset, walls, T, T

' the hero: a 12x12 yellow square
DIM hero = SPRITE.CREATE(12, 12)
SPRITE.SETBUFFER hero, RESHAPE([255, 210, 0, 255], [12 * 12 * 4])
SPRITE.POS hero, 2 * T + 2, 2 * T + 2

CAM.FOLLOW hero
CAM.BOUNDS 0, 0, COLS * T, ROWS * T

DIM running = TRUE
DIM lastT = TICK()
DIM now = 0.0
DIM dt = 0.0
DIM ox = 0.0
DIM oy = 0.0
DIM dx = 0.0
DIM dy = 0.0
DO WHILE running
    now = TICK()
    dt = (now - lastT) / 1000.0
    lastT = now
    IF GFX.KEYSTATE("Escape") THEN running = FALSE

    ox = SPRITE.GET_X(hero)
    oy = SPRITE.GET_Y(hero)
    dx = (GFX.KEYSTATE("Right") - GFX.KEYSTATE("Left")) * 90 * dt
    dy = (GFX.KEYSTATE("Down") - GFX.KEYSTATE("Up")) * 90 * dt
    ' move one axis at a time and undo the step that hits a wall
    SPRITE.POS hero, ox + dx, oy
    IF TILEMAP.COLLIDES(hero, "walls") THEN SPRITE.POS hero, ox, oy
    SPRITE.POS hero, SPRITE.GET_X(hero), oy + dy
    IF TILEMAP.COLLIDES(hero, "walls") THEN SPRITE.POS hero, SPRITE.GET_X(hero), oy

    SPRITE.UPDATE
    CLS 0, 0, 0
    TILEMAP.DRAW "ground"
    TILEMAP.DRAW "walls"
    SPRITE.DRAW_ALL
    TEXT 8, 8, "Arrows move, Esc quits", 255, 255, 255
    SCREENFLIP
    SLEEP 16
LOOP
```

Moving one axis at a time lets the hero slide along a wall instead of
sticking to it when a key for the other direction is also held.

### Animation from a sprite sheet

`SPRITE.LOAD(file$, frame_w, frame_h)` cuts an image into frames.
`SPRITE.ANIM` names a sequence of frame numbers with a speed in frames per
second, and `SPRITE.PLAY` starts it. `SPRITE.UPDATE` advances it.

```basic
SCREEN 320, 200, "Animation"

' a 16x32 sheet: frame 0 red, frame 1 blue
DIM sheet = SPRITE.CREATE(16, 32)
SPRITE.SETBUFFER sheet, APPEND(RESHAPE([230, 60, 60, 255], [16 * 16 * 4]), RESHAPE([60, 90, 230, 255], [16 * 16 * 4]))
SPRITE.SAVE sheet, "blink.png"
SPRITE.DELETE sheet

DIM blinker = SPRITE.LOAD("blink.png", 16, 16)
SPRITE.SCALE blinker, 4
SPRITE.POS blinker, 128, 68
SPRITE.ANIM blinker, "blink", [0, 1], 4
SPRITE.PLAY blinker, "blink"

DO UNTIL GFX.KEYSTATE("Escape")
    SPRITE.UPDATE
    CLS 20, 20, 20
    SPRITE.DRAW_ALL
    SCREENFLIP
    SLEEP 16
LOOP
```

A real sheet has one row per direction (walk down, left, right, up), and
the game switches clips with `SPRITE.PLAY` when the direction changes.
`jdb/demos/games/vibe_game.jdb` does this for a Pac-Man-style hero.

### More sprite and map tools

* Collisions between sprites: `SPRITE.COLLISION(a, b)` for two sprites, or
  `SPRITE.GROUP` with `SPRITE.COLLISIONS(group1$, group2$)` for whole groups
  (all bullets against all enemies).
* Simple physics: `SPRITE.VELOCITY`, `SPRITE.GRAVITY` and `SPRITE.LAND` for a
  platformer jump.
* Maps made in the [Tiled](https://www.mapeditor.org/) editor: `TILED.LOAD`
  reads a `.tmx` file, `TILED.DRAW` draws it with the camera, and
  `TILED.OBJECTS` returns the object layer (spawn points, doors).
* Effects: `PARTICLE.EMIT` and `PARTICLE.DRAW` for sparks and explosions,
  `CAM.SHAKE` for a hit.

The reference sections are [Sprites](../languages.md#sprites),
[Tiled Maps](../languages.md#tiled-maps-tiled),
[Programmatic Tilemaps](../languages.md#programmatic-tilemaps-tilemap),
[Camera](../languages.md#camera-cam) and
[Particles](../languages.md#particles-particle).

## 5. Sound

jdBasic has two sound systems, and a game can use both.

`SOUND.*` is a synthesizer: you set up a voice with a waveform and an
envelope, then play notes on it. It needs no sound files, which suits
retro effects.

```basic
SOUND.INIT
SOUND.VOICE 0, "SQUARE", 0.005, 0.05, 0.0, 0.04
SOUND.VOICE 1, "NOISE", 0.005, 0.25, 0.0, 0.2
SOUND.PLAY 0, "g6"
SLEEP 150
SOUND.PLAY 1, "c2"
SLEEP 400
SOUND.RELEASE 0
SOUND.RELEASE 1
SOUND.SHUTDOWN
PRINT "done"
```

Voice 0 is a short laser blip, voice 1 a noise burst for an explosion.
`SOUND.VOICE track, waveform$, attack, decay, sustain, release` sets the
envelope in seconds; with a sustain of 0 the note fades out by itself. Call
`SOUND.RELEASE` on the effect tracks now and then so finished notes free
their voice. The same system plays background music from patterns with
`SOUND.NOTE` and `SOUND.BPM`; see the [Sequencer Help](../SequencerHelp.md).

`AUDIO.*` plays sound files: WAV or OGG effects with `AUDIO.LOADWAV` and
`AUDIO.PLAY`, music with `AUDIO.LOADMUS` and `AUDIO.PLAYMUS`. This example
first writes a short WAV file with `WAV.WRITE` (needs the `FX` build flag),
then plays it:

```basic
' a 0.1 second falling blip, written to a WAV file
DIM t = IOTA(4410, 0) / 44100
DIM blip = SIN(2 * MATH.PI * (900 - 3000 * t) * t) * (1 - t * 10) * 0.5
DIM ok = WAV.WRITE("blip.wav", blip)

AUDIO.INIT
DIM sfx = AUDIO.LOADWAV("blip.wav")
AUDIO.PLAY sfx
SLEEP 300
AUDIO.CLOSE
PRINT "done"
```

In a game, load every effect once before the loop and call `AUDIO.PLAY`
where the event happens, for example on the paddle hit. Both calls return at
once, so the frame does not wait for the sound. More in
[Audio File Playback](../languages.md#audio-file-playback-sdl_mixer),
[Sound](../languages.md#sound) and, for effect chains,
[Audio FX](../AudioFX.md).

## 6. Compiling a game to .exe

Graphics programs compile like any other program:
`jdBasic.exe -c paddle.jdb` writes `paddle.exe` next to the source and copies
`jdbrt.dll` and the SDL DLLs beside it, so the folder can be zipped and run on
another Windows PC. The compiler always works in STRICT and EXPLICIT mode:
every variable needs a `DIM`, with a type where it matters, and that is why
the Paddle game declares everything up front. The Paddle, game loop and tile
map programs in this guide all compile as they are. The details, and what to
ship, are in [Compiling to a native .exe](./native.md).

## 7. Bigger examples to read

* [Vallys Reise](../../fluppi/README.md): a complete top-down RPG of about
  9,000 lines with Tiled maps, a party, turn-based battles, quests and music
  from the `SOUND.*` sequencer. Run `fluppi/rpg_demo.jdb`; the engine is in
  `fluppi/RPG_ENGINE.jdb`.
* [space_shooter.jdb](../../jdb/demos/games/space_shooter/space_shooter.jdb):
  *Stellar Drift*, a vector shooter. A good model for `GFX.KEYSTATE` input,
  `ON "QUIT"` and synthesized music plus effects on separate voices.
* [tilt.jdb](../../jdb/demos/games/tilt.jdb): a falling-block game with ship
  physics, title and options screens.
* [prisma.jdb](../../jdb/demos/games/prisma.jdb): a match-3 game with mouse
  input and animations.
* [vibe_game.jdb](../../jdb/demos/games/vibe_game.jdb): sprite sheets made
  with `SPRITE.CREATE`, `SPRITE.ANIM` clips per direction, and gamepad input.
* [invictus.jdb](../../jdb/demos/games/invictus/invictus.jdb): a two-player
  arena duel.
* [parallax_demo.jdb](../../jdb/demos/games/parallax_demo.jdb) and
  [rotate_demo.jdb](../../jdb/demos/games/rotate_demo.jdb): short demos of
  layered scrolling and `SPRITE.ROTATE` with a pivot.
* [The Apple II emulator](../../jdb/README.md#6502--apple-ii-emulator-jdbemu):
  a 6502 CPU and Apple II front-end in jdBasic, with
  [emu_run.jdb](../../jdb/emu/emu_run.jdb) drawing the text screen.

The [demo index](../../jdb/README.md) lists more games and graphics programs.

## Where next

* [Graphics and Multimedia Functions](../languages.md#graphics-and-multimedia-functions):
  every drawing, image, window and input function.
* [Desktop applications](./gui.md): ImGui panels inside the same `SCREEN`
  window, for editors and debug overlays.
* [Compiling to a native .exe](./native.md): shipping a game.
* [Sequencer Help](../SequencerHelp.md) and [Audio FX](../AudioFX.md): music
  and sound design.
* [Tour](../tour.md): the language itself, if a construct in the examples
  was new.
