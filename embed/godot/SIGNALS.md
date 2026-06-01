# GODOT signals - connecting Godot signals to jdBasic SUBs

Godot is event-driven: buttons emit `pressed`, timers emit `timeout`, areas emit `body_entered`. `GODOT.CONNECT` wires any such signal straight to a jdBasic SUB, so a pure-jdBasic script can react to engine events without a GDScript relay and without polling.

These natives are registered automatically on every script VM, alongside `GODOT.CALL` / `GODOT.GET` / `GODOT.SET`.

## Functions

| Function | Returns | Notes |
|----------|---------|-------|
| `GODOT.CONNECT(obj, "signal", "sub" [, flags])` | bool | Wire a signal on `obj` to `sub`. 1 on success, 0 if the handle is bad |
| `GODOT.DISCONNECT(obj, "signal", "sub")` | bool | Remove that wiring. 1 if a match was found |
| `GODOT.EMIT(obj, "signal" [, args...])` | bool | Fire a signal yourself (the other direction) |

`obj` is a bridge handle (from `GODOT.SELF`, `GODOT.CALL(... "get_node" ...)`, etc.). `sub` is the name of a SUB in the running script.

`flags` is Godot's `CONNECT_*` bitmask: `1` = DEFERRED, `4` = ONE_SHOT. Omit it for the normal immediate connection.

A duplicate `CONNECT` of the same `(obj, signal, sub)` is ignored, so calling it again from a re-run `_ready` will not stack handlers.

## Signal arguments

Whatever the signal carries is passed to the SUB, marshalled the same way `GODOT.GET` return values are:

| Signal arg type | Arrives in jdBasic as |
|-----------------|-----------------------|
| int / float / bool / String | the scalar |
| Vector2 / Vector3 | `[x, y]` / `[x, y, z]` |
| Color | `[r, g, b, a]` |
| Object (Node, etc.) | a bridge handle - feed it straight back into `GODOT.GET/SET/CALL` |
| Dictionary / packed arrays | not marshalled yet - arrive as `0` |

So `body_entered(body)` hands your SUB a handle you can immediately query with `GODOT.GET(body, "name")`.

## Example

```basic
EXTENDS Node2D

DIM self_h = 0
DIM clicks = 0

SUB on_click()
    clicks = clicks + 1
    IF clicks >= 5 THEN
        GODOT.DISCONNECT(GODOT.CALL(self_h, "get_node", "UI/Button"), "pressed", "on_click")
    ENDIF
ENDSUB

SUB on_slide(v)         ' HSlider value_changed passes the new value
    GODOT.SET(GODOT.CALL(self_h, "get_node", "UI/Status"), "text", "slider: " + STR$(v))
ENDSUB

SUB _ready()
    self_h = GODOT.SELF()
    GODOT.CONNECT(GODOT.CALL(self_h, "get_node", "UI/Button"), "pressed", "on_click")
    GODOT.CONNECT(GODOT.CALL(self_h, "get_node", "UI/Slider"), "value_changed", "on_slide")
ENDSUB
```

See `godot/jd-one/connect_demo.tscn` for a runnable version, and `connect_smoke.tscn` for the headless regression check.

## Re-entrancy

The jdBasic interpreter is single-threaded and not re-entrant. If a signal fires *while* the VM is already running an engine callback (e.g. your handler calls `emit_signal`, or `add_child` triggers `child_entered_tree`), the dispatch is **queued** and drained the moment the outer callback returns - never nested. Handlers triggered this way run in order, one after another, on the main thread. The common case (a button or timer firing between frames) runs immediately with no delay.

## Lifetime

Connections are owned by the script's bridge. On hot-reload or when the node's script detaches, every connection this script made is dropped automatically, so a Node that outlives the script never holds a callable pointing at a dead VM.
