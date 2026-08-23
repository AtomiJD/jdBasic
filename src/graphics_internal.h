#pragma once
#ifdef GFX
#include <SDL3/SDL.h>
#include "sprites.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>

// Shared graphics-subsystem state. Accessible to sprites.cpp (and any
// future sibling translation unit that needs the SDL renderer/texture
// cache without going through the full builtin dispatch).
//
// graphics.cpp owns the lifecycle of these (SCREEN creates the window
// and renderer, FREE/QUIT tears them down). Everyone else reads-only
// or appends to g_images.

extern SDL_Window*   g_window;
extern SDL_Renderer* g_renderer;
extern Uint8 g_draw_r, g_draw_g, g_draw_b, g_draw_a;
extern std::unordered_map<int, SDL_Texture*> g_images;
extern int g_next_image_id;       // shared id counter across IMG.LOAD + SPRITE.LOAD
extern int g_screen_w;
extern int g_screen_h;

// Camera state - owned by graphics.cpp, read/written by sprites.cpp
// (SPRITE.UPDATE drives follow + shake decay, SPRITE.DRAW_ALL reads
// the offset, the CAM.* builtins push values back).
extern Camera g_cam;

// Particle state - SPRITE.UPDATE integrates physics on every tick;
// PARTICLE.* builtins in graphics.cpp own emit/draw/clear/count.
struct Particle {
    float x, y, vx, vy;
    float life, max_life;        // seconds
    Uint8 r, g, b;
    float size;
    float gravity;
};
extern std::vector<Particle> g_particles;

// Sprite + animation state - owned by sprites.cpp, but graphics.cpp
// reads Sprite fields directly for TILEMAP.COLLIDES / TILED.COLLIDES.
extern std::map<int, Sprite> g_sprites;
extern int g_next_sprite_id;

// Defined in sprites.cpp - keep external so the tilemap collision
// builtins in graphics.cpp can resolve a sprite handle the same way.
Sprite& get_sprite(const char* fn, int id);

void ensure_screen(const char* fn);
void apply_draw_color();

// Resolve a user-supplied filesystem path: pass absolute paths through
// untouched, and for relative ones try CWD first, then fall back to a
// path relative to the main script's directory.
std::string resolve_asset_path(const std::string& p);

#endif
