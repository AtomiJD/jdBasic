---
title: Train jdBasic - Lesson 09 - Graphics
hook: Pixels on screen with three commands
tags: graphics, sdl, drawing, circle, rgb, beginner, lesson-9
---

Lesson 09 of Train jdBasic - eight lessons in the text terminal, time for
a window. SDL-backed graphics with a tiny surface: one command to open
a window, one to set colour, a handful for the shapes themselves.

## What you'll learn

- `SCREEN width, height, title$` opens a window
- `DRAWCOLOR r, g, b` sets the current colour (0..255 RGB)
- `RECT`, `CIRCLE`, `LINE` for the basic shapes (with a `TRUE` flag for filled)
- `SCREENFLIP` pushes the back-buffer to the display
- A short render loop (`GFX.POLLEVENT()` + `SLEEP`) keeps the window responsive
- Because drawing commands are regular statements, you can put them in FOR loops for patterns

## Code from the lesson

```basic
SCREEN 600, 400, "Shapes"
DRAWCOLOR 30, 30, 50
RECT 0, 0, 600, 400, TRUE
DRAWCOLOR 80, 200, 255
RECT 100, 100, 400, 200, TRUE
DRAWCOLOR 255, 100, 100
CIRCLE 300, 200, 60, TRUE
DRAWCOLOR 255, 255, 255
LINE 0, 0, 600, 400
LINE 600, 0, 0, 400
SCREENFLIP
```

Dark background, blue panel, red circle, two white diagonals - each
command paints over what came before, so order matters.

## Next up

Lesson 10 (final episode) - writing your own module, importing it, and a
small project that ties everything together.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
