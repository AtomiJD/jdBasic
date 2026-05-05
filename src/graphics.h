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

// REPL workspace-switch hook. When the REPL is host (not standalone
// `jdbasic foo.jdb`), Console::run installs a callback so Ctrl+F1..F4
// pressed in an SDL window switches workspaces instead of being eaten by
// ImGui or the running program. Standalone leaves the hook null and the
// SDL loops short-circuit on the first branch — zero cost.
void gfx_set_repl_switch_hook(bool active, void (*cb)(int));
