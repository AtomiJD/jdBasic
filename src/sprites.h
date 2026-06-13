#pragma once
#ifdef GFX
class VM;

// Register all SPRITE.* native builtins onto the VM. Called once from
// register_graphics_builtins(). The sprite registry, animation state,
// and SDL-texture-rotated draw path live entirely inside sprites.cpp.
void register_sprite_builtins(VM& vm);

// Resolve a sprite id to its underlying SDL texture (for ImGui::Image via
// GUI.IMAGE). Returns nullptr for an unknown id. Fills out_w/out_h with the
// texture's pixel size when non-null.
struct SDL_Texture;
SDL_Texture* sprite_texture(int id, int* out_w, int* out_h);

#endif
