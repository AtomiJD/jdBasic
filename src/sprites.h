#pragma once
#include <string>
#include <vector>

// The sprite model itself carries no platform: position, motion, frames,
// animation, physics and collision group are the same on a desktop with
// SDL behind it and on a board that blits into its own framebuffer. Only
// getting pixels in and putting them on a screen differs, and that lives
// behind the backend hooks at the bottom of this file.

struct SpriteAnim {
    std::string name;
    std::vector<int> frames;   // frame indices into the spritesheet
    float fps;                 // playback speed
    bool loop;                 // loop animation (default true)
};

struct Sprite {
    int texture_id;           // references the backend's image store
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

// Where the world is being watched from. Also pure arithmetic, and
// SPRITE.UPDATE follows and shakes it, so it belongs beside the sprites
// rather than in the SDL header.
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

#if defined(GFX) || defined(PICO)
class VM;

// Register all SPRITE.* native builtins onto the VM.
void register_sprite_builtins(VM& vm);
#endif

#ifdef GFX
// Resolve a sprite id to its underlying SDL texture (for ImGui::Image via
// GUI.IMAGE). Returns nullptr for an unknown id. Fills out_w/out_h with the
// texture's pixel size when non-null.
struct SDL_Texture;
SDL_Texture* sprite_texture(int id, int* out_w, int* out_h);
#endif
