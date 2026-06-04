# GDX.INPUT - Native Input API for jdBasic-in-Godot

`GDX.INPUT.*` is a suite of native functions registered automatically on every `JDBasicVM` instance. It exposes Godot's `Input` singleton (and `DisplayServer` for mouse position) directly to jdBasic scripts, no GDScript pump needed.

Two halves: stateless polling, and an event queue for discrete one-shot signals.

## Polling

Direct reads of Godot's input state. No setup; just call.

| Function | Returns | Notes |
|----------|---------|-------|
| `GDX.INPUT.IS_ACTION_PRESSED("name")` | bool | Held-down state of a project-settings action |
| `GDX.INPUT.IS_ACTION_JUST_PRESSED("name")` | bool | True only for the frame the press happened |
| `GDX.INPUT.IS_ACTION_JUST_RELEASED("name")` | bool | True only for the frame the release happened |
| `GDX.INPUT.GET_ACTION_STRENGTH("name")` | double | 0..1, useful for analog inputs |
| `GDX.INPUT.GET_AXIS("neg", "pos")` | double | -1..1, classic axis pair |
| `GDX.INPUT.GET_VECTOR("l", "r", "u", "d")` | `[x, y]` | Deadzone-aware joypad vector |
| `GDX.INPUT.IS_KEY_PRESSED(keycode)` | bool | Raw key, Godot Key enum integer |
| `GDX.INPUT.MOUSE_POSITION()` | `[x, y]` | Global screen coords (DisplayServer) |
| `GDX.INPUT.MOUSE_VELOCITY()` | `[x, y]` | Last frame's mouse delta |
| `GDX.INPUT.IS_MOUSE_BUTTON_PRESSED(idx)` | bool | 1 = left, 2 = right, 3 = middle |
| `GDX.INPUT.SET_MOUSE_MODE(mode)` | - | 0 visible, 1 hidden, 2 captured, 3 confined, 4 confined+hidden |

### Example: WASD movement in jdBasic

```basic
EXPORT FUNC tick_player(dt)
    DIM v = GDX.INPUT.GET_VECTOR("ui_left", "ui_right", "ui_up", "ui_down")
    DIM speed = 5.0
    DIM sprint = 1.0
    IF GDX.INPUT.IS_ACTION_PRESSED("sprint") THEN sprint = 2.0
    player_x = player_x + v[0] * speed * sprint * dt
    player_z = player_z + v[1] * speed * sprint * dt
    IF GDX.INPUT.IS_ACTION_JUST_PRESSED("jump") THEN
        player_vy = 6.0
    ENDIF
ENDFUNC
```

## Event queue

Polling is great for movement but awkward for discrete actions like "the player just opened a chest." The event queue lets you forward `InputEvent` from a GDScript `_input(event)` hook and read them on the jdBasic side as records.

**GDScript side** (any node with `_input`):

```gdscript
@onready var vm: JDBasicVM = JDBasicVM.new()

func _input(event: InputEvent) -> void:
    if event is InputEventAction:
        var ev := event as InputEventAction
        vm.push_input_event("action", ev.action,
                            "pressed" if ev.pressed else "released",
                            ev.strength)
    elif event is InputEventKey and event.pressed and not event.echo:
        vm.push_input_event("key", "",
                            "pressed",
                            float(event.keycode))
```

**jdBasic side**:

```basic
DIM ev = GDX.INPUT.POLL_EVENT()
DO WHILE ev <> NONE
    IF ev{"kind"} = "action" ANDALSO ev{"action"} = "talk" ANDALSO ev{"type"} = "pressed" THEN
        on_player_talk()
    ENDIF
    IF ev{"kind"} = "key" ANDALSO INT(ev{"strength"}) = 27 THEN
        on_escape()
    ENDIF
    ev = GDX.INPUT.POLL_EVENT()
LOOP
```

Each event is a MAP with four keys:

| Key | Meaning |
|-----|---------|
| `kind` | `"action"`, `"key"`, `"mouse"` - whatever the GDScript side tagged it with |
| `action` | the action name, empty for non-action events |
| `type` | `"pressed"` or `"released"` |
| `strength` | 0..1 for actions, keycode for keys, button index for mouse buttons |

Plus a queue-size accessor:

```basic
DIM n = GDX.INPUT.PENDING_EVENTS()
PRINT "queue has "; n; " events"
```

The queue lives in the JDBasicVM C++ side and is thread-safe (events can be pushed from a different thread than the one running jdBasic). It rolls over without bound, so make sure your jdBasic side drains it every frame.

## When to use which

- **Polling** for continuous state: movement, camera, sprint, axis-based aim.
- **Event queue** for one-shots: open menu, interact, jump, fire.
- **Mix freely** - they read different sides of the same input state and do not interfere.
