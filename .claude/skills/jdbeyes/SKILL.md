---
name: jdbeyes
description: Give Claude "eyes" and "hands" on jdBasic output - capture a window, screen, 2D GFX frame, OpenGL frame, or TUI layout to a file and Read it, and drive a running window with synthetic mouse and keyboard input over the FFI. Use whenever you need to VISUALLY verify a GUI / graphics / 3D / TUI result instead of guessing from exit codes (rpg-native, sprites, tilemaps, OpenGL/3D demos, the livecoder, Godot-embed windows, any on-screen window), or to test a gesture, drag or pointer sequence that no headless test can reach. Windows; runs via the jdBasic MCP (jdb_eval) or build/jdBasic.exe.
---

# jdbeyes - see jdBasic (and any window) output

Claude is text-only, but the **Read tool renders PNG/JPG visually**. So: capture
to an image file, then `Read` it. Four channels, pick by what you're verifying.

## 1. OS window / whole screen / window content
```basic
OS.SCREENSHOT("D:/usr/dev/cc/tmp/shot.png")                 ' whole virtual desktop
OS.SCREENSHOT("...png", "window")                            ' foreground window (incl. frame)
OS.SCREENSHOT("...png", "window", "IMG_9311.JPG")            ' a window by FindWindow title
OS.SCREENSHOT("...png", "client", "Calculator")              ' a window's content only
```
Returns `0` on success, `<0` on error (e.g. `-2` window not found). Format from the
extension (`.png/.jpg/.bmp`). Windows-only. Use for: launched GUI apps, Godot-embed,
the actual desktop. **It captures the user's real screen - stay task-relevant.**

## 2. jdBasic 2D renderer (SCREEN / RECT / SPRITE / tilemap)
```basic
SCREEN 360, 240
' ... DRAWCOLOR / RECT / CIRCLE / SPRITE / TEXT ...
GFX.SAVE_SCREENSHOT("D:/usr/dev/cc/tmp/gfx.png")   ' <-- BEFORE SCREENFLIP
GFX.CLOSE
```
**Gotcha: capture BEFORE `SCREENFLIP`** - after the swap the back buffer is cleared
(you'd get a black frame). Optional region: `GFX.SAVE_SCREENSHOT(p$, x, y, w, h)`.

## 3. OpenGL / 3D (GL.WINDOW)
```basic
GL.WINDOW(320, 240, "scene")
' ... GL.CLEAR / shaders / GL.DRAW.TRIS ...
GL.SAVE_SCREENSHOT("D:/usr/dev/cc/tmp/gl.png")     ' <-- BEFORE GL.FLIP
GL.CLOSE()
```
Same rule: **before `GL.FLIP`**. (glReadPixels of the back buffer, flipped top-down.)

## 4. TUI layout (no terminal needed)
```basic
TUI.BEGIN "title" : ... build widgets ... : TUI.END
PRINT TUI.RENDER_HEADLESS$(100, 30)        ' one frame as text -> stdout
```

## 5. Hands: drive a window that is already running

Eyes alone cannot test a gesture. jdBasic reaches `user32.dll` over the FFI, so a
**second** jdBasic process can move the cursor, press the button and type into a
window the first one owns - then photograph what it did (channel 1).

```basic
DECLARE FUNC FindWindowA LIB "user32.dll" ALIAS "FindWindowA" (clsptr AS INTEGER, title AS STRING) AS INTEGER
DECLARE FUNC MoveWindow LIB "user32.dll" ALIAS "MoveWindow" (hwnd AS INTEGER, x AS INTEGER, y AS INTEGER, w AS INTEGER, h AS INTEGER, rp AS INTEGER) AS INTEGER
DECLARE FUNC SetForegroundWindow LIB "user32.dll" ALIAS "SetForegroundWindow" (hwnd AS INTEGER) AS INTEGER
DECLARE FUNC SetCursorPos LIB "user32.dll" ALIAS "SetCursorPos" (x AS INTEGER, y AS INTEGER) AS INTEGER
DECLARE FUNC mouse_event LIB "user32.dll" ALIAS "mouse_event" (flags AS INTEGER, dx AS INTEGER, dy AS INTEGER, data AS INTEGER, extra AS INTEGER) AS INTEGER
DECLARE FUNC keybd_event LIB "user32.dll" ALIAS "keybd_event" (vk AS INTEGER, scan AS INTEGER, flags AS INTEGER, extra AS INTEGER) AS INTEGER

CONST LEFTDOWN = 2
CONST LEFTUP = 4
CONST KEYUP = 2                       ' keybd_event flag; omit it for key-down

DIM hwnd = FindWindowA(0, "jdVOID")   ' 0, NOT "" - see the traps below
DIM mv = MoveWindow(hwnd, 0, 0, 484, 940, 1)
DIM fg = SetForegroundWindow(hwnd)

' a tap in client coordinates (frame offset added)
DIM p = SetCursorPos(8 + 234, 31 + 358)
DIM d = mouse_event(LEFTDOWN, 0, 0, 0, 0)
DIM u = mouse_event(LEFTUP, 0, 0, 0, 0)
```

A **stroke** (drag, swipe, lasso) is the button held down while `SetCursorPos` walks
a path, with a short `SLEEP` per step so the target's frame loop samples the motion:

```basic
DIM dn = mouse_event(LEFTDOWN, 0, 0, 0, 0)
FOR i = 1 TO 12
    DIM a = i * 2 * PI / 12
    DIM sp = SetCursorPos(8 + cx + INT(r * COS(a)), 31 + cy + INT(r * SIN(a)))
    SLEEP 35
NEXT i
DIM up = mouse_event(LEFTUP, 0, 0, 0, 0)
```

### Traps that cost a round each
- **`FindWindowA(0, title)`** - the class argument must be a NULL pointer. Declare it
  `AS INTEGER` and pass `0`; an empty string `""` is a class *named* "", which matches
  nothing and returns 0.
- **`cls AS STRING` does not parse** - `CLS` is a builtin. Name FFI parameters
  `clsptr` / `clsname`.
- **Know where the client area starts.** `MoveWindow` positions the *outer* frame, so
  put the window at 0,0 and add the border: on this desktop **8 px sideways, 31 px
  top**. Verify once by capturing `"client"` and comparing against a click you can
  see land (a selected card, a highlighted button) before trusting the offsets.
- **The world moves while you click.** Synthetic input costs tens of milliseconds per
  step, and a running game does not wait: a unit walked half the board between the
  drop and the loop drawn around it. Keep the sleeps as short as the target's frame
  (33 ms at 30 fps), or drive something that stands still.
- **It takes over the real cursor.** Windows cannot tell injected input from a hand on
  the mouse. Say so before starting, keep it to a few seconds, and never leave a
  button down.

Use it for what no headless test reaches: gestures, drag and drop, pointer sequences,
"the mode is gone after the first release" bugs. The rules underneath belong in a
headless test - this is for the hands, not the arithmetic.

## How to run
- **Via MCP** (persistent VM, easiest): `jdb_eval` the capture call, then `Read` the file.
  `OS.SCREENSHOT` uses no subprocess so it won't deadlock the stdio server.
- **Via Bash**: `build/jdBasic.exe script.jdb` (a separate process; opens a real window
  for GFX/GL). For a `-c` compiled exe, copy `build/*.dll` next to it first.
- Then **`Read tmp/shot.png`** - that's the actual seeing step.

## Tips
- Use **absolute paths** (or know the CWD): the MCP server's CWD is `mcp-runtime/`,
  a `-c` exe's CWD is wherever it runs - a bare `tmp/x.png` may land in the wrong place.
- Delete throwaway captures from `tmp/` when done.
- A solid-black/empty PNG almost always means "captured after the flip" - move the
  save call before SCREENFLIP / GL.FLIP.
