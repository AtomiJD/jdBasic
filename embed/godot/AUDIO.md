# GDX.AUDIO - sound and music for jdBasic-in-Godot

A thin convenience layer over `AudioStreamPlayer` so a pure-jdBasic game can
play sound effects and music without wiring nodes by hand. The natives work
with real player objects internally, so you never have to pass an audio
stream around as a bridge handle.

Registered automatically on every script VM.

| Function | Returns | Notes |
|----------|---------|-------|
| `GDX.AUDIO.PLAY(path [, volume_db [, pitch]])` | handle | Fire-and-forget SFX; the player frees itself when the sound finishes |
| `GDX.AUDIO.MUSIC(path [, volume_db])` | handle | Looping music on one reusable player; calling it again swaps the track |
| `GDX.AUDIO.STOP_MUSIC()` | - | Stop and free the music player |
| `GDX.AUDIO.STOP(handle)` | bool | Stop and free any player a PLAY/MUSIC call returned |

`path` is a `res://` path to an imported audio file (`.wav`, `.ogg`, `.mp3`).
`volume_db` defaults to `0` (full); negative values attenuate (e.g. `-6`).
`pitch` defaults to `1.0`.

The player nodes are parented to the script's own Node, so they go away with
it. SFX players also self-free the moment their sound ends, so rapid-fire
`GDX.AUDIO.PLAY` calls (footsteps, shots) don't pile up.

Music loops format-agnostically: the player replays from the top on its
`finished` signal, so you don't have to set a loop flag on the stream.

## Example

```basic
SUB on_hit()
    GDX.AUDIO.PLAY("res://sfx/hit.wav", -3.0)
ENDSUB

SUB _ready()
    self_h = GDX.SELF()
    GDX.AUDIO.MUSIC("res://music/theme.ogg", -8.0)   ' starts looping
ENDSUB

SUB on_game_over()
    GDX.AUDIO.STOP_MUSIC()
    GDX.AUDIO.PLAY("res://sfx/lose.wav")
ENDSUB
```

> Audio files must be imported by Godot before they load. Opening the
> project in the editor once does this; for a headless/CI run, do a
> `godot --headless --import` pass first.
