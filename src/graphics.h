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
