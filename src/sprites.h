#pragma once
#ifdef GFX
class VM;

// Register all SPRITE.* native builtins onto the VM. Called once from
// register_graphics_builtins(). The sprite registry, animation state,
// and SDL-texture-rotated draw path live entirely inside sprites.cpp.
void register_sprite_builtins(VM& vm);

#endif
