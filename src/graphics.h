#pragma once
#include <cstdint>
#include <string>

class VM;
void register_graphics_builtins(VM& vm);

// For integration from vm.cpp / main.cpp
bool gfx_is_active();
void gfx_clear(uint8_t r, uint8_t g, uint8_t b);
void gfx_shutdown();

// GFX-mode INKEY$ support
bool gfx_has_key();
std::string gfx_get_key();

// Console pause-wait. Called from main.cpp's run_source when vm.run()
// returned with the VM in a stopped state (STOP statement hit, no
// MCP host to resume it). Pumps SDL events to keep the window alive
// and returns when:
//   true  -> user pressed Space / Enter / F7 in the game window,
//            OR gfx_signal_resume() was called from another thread
//            (REPL's `RESUME` command path)
//   false -> user closed the window (intent: exit the program)
bool gfx_console_pause_wait();

// Cross-thread signal: ask the wait-loop to return as if the user
// pressed a resume key. Used by the REPL when the user types
// `RESUME` while the script's worker thread is parked in the wait
// loop. Idempotent; clears itself on consumption.
void gfx_signal_resume();

// Pump SDL events without consuming them. Safe to call from the
// REPL main thread while a worker-owned SDL window is in STOP
// state - keeps the window responsive (avoids the Windows "Not
// Responding" ghost) so the user can read the script's paused
// overlay while typing RESUME or inspecting state in the REPL.
// No-op when no window exists.
void gfx_pump_events();

// REPL workspace-switch hook. When the REPL is host (not standalone
// `jdbasic foo.jdb`), Console::run installs a callback so Ctrl+F1..F4
// pressed in an SDL window switches workspaces instead of being eaten by
// ImGui or the running program. Standalone leaves the hook null and the
// SDL loops short-circuit on the first branch - zero cost.
void gfx_set_repl_switch_hook(bool active, void (*cb)(int));
