#pragma once
#ifdef GFX
#include <SDL3/SDL.h>
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

// Camera state — owned by graphics.cpp, read/written by sprites.cpp
// (SPRITE.UPDATE drives follow + shake decay, SPRITE.DRAW_ALL reads
// the offset, the CAM.* builtins push values back).
struct Camera {
    float x = 0, y = 0;
    int follow_id = -1;
    float smooth = 0.1f;           // 0=instant, higher=smoother
    float bounds_x = 0, bounds_y = 0, bounds_w = 0, bounds_h = 0;
    bool has_bounds = false;
    float shake_intensity = 0;
    float shake_timer = 0;
    float shake_ox = 0, shake_oy = 0;
};
extern Camera g_cam;

// Particle state — SPRITE.UPDATE integrates physics on every tick;
// PARTICLE.* builtins in graphics.cpp own emit/draw/clear/count.
struct Particle {
    float x, y, vx, vy;
    float life, max_life;        // seconds
    Uint8 r, g, b;
    float size;
    float gravity;
};
extern std::vector<Particle> g_particles;

// Sprite + animation state — owned by sprites.cpp, but graphics.cpp
// reads Sprite fields directly for TILEMAP.COLLIDES / TILED.COLLIDES.
struct SpriteAnim {
    std::string name;
    std::vector<int> frames;   // frame indices into the spritesheet
    float fps;                 // playback speed
    bool loop;                 // loop animation (default true)
};

struct Sprite {
    int texture_id;           // references g_images
    float x, y;               // position
    float vx, vy;             // velocity (pixels/sec)
    float scale_x, scale_y;   // scale (1.0 default)
    float origin_x, origin_y; // anchor point (default 0,0 = top-left)
    float angle;              // rotation in degrees
    int alpha;                // 0-255 (255 = fully opaque)
    bool visible;
    bool flip_h, flip_v;
    float tex_w, tex_h;       // full texture dimensions
    float w, h;               // display dimensions (frame_w/h or tex_w/h)
    std::string group;        // collision group name (empty = no group)
    // Spritesheet
    int frame_w, frame_h;     // 0 = use full texture
    int cols;                 // frames per row in spritesheet
    int current_frame;
    // Animation
    int zorder;               // draw order (lower = behind)
    // Animation
    std::vector<SpriteAnim> anims;
    int current_anim;         // -1 = none
    float anim_timer;         // accumulated time
    bool playing;
    // Physics
    float gravity;            // pixels/sec² (0 = disabled)
    bool on_ground;
};

extern std::map<int, Sprite> g_sprites;
extern int g_next_sprite_id;

// Defined in sprites.cpp — keep external so the tilemap collision
// builtins in graphics.cpp can resolve a sprite handle the same way.
Sprite& get_sprite(const char* fn, int id);

void ensure_screen(const char* fn);
void apply_draw_color();

// Resolve a user-supplied filesystem path: pass absolute paths through
// untouched, and for relative ones try CWD first, then fall back to a
// path relative to the main script's directory.
std::string resolve_asset_path(const std::string& p);

#endif
