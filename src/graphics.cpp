#include "graphics.h"
#include "tiledmap.h"
#ifdef IMGUI
#include "gui.h"
#include "imgui.h"
#endif
#include "vm.h"
#include "errors.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <SDL_mixer.h>
#include <cmath>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Defined in main.cpp (exe) and vm_bridge.cpp (libjdbrt.so) — the
// directory of the .jdb the user invoked. Falls back to "." in the
// runtime DLL until the entry point sets it.
extern std::string g_base_dir;

// Resolve a user-supplied filesystem path: pass absolute paths through
// untouched, and for relative ones try CWD first (back-compat: most
// existing demos chdir before run), then fall back to a path relative
// to the main script's directory. Returns the path that actually exists,
// or the original input if neither did (so the caller still produces a
// recognisable error message).
static std::string resolve_asset_path(const std::string& p) {
    namespace fs = std::filesystem;
    fs::path in(p);
    if (in.is_absolute()) return p;
    std::error_code ec;
    if (fs::exists(in, ec)) return p;
    if (!g_base_dir.empty()) {
        fs::path candidate = fs::path(g_base_dir) / in;
        if (fs::exists(candidate, ec)) return candidate.string();
    }
    return p;
}

// ── Global SDL state ────────────────────────────────────────────

static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;

// Streaming texture used by GFX.PLOT_POINTS_TEX. File-scope so it can be
// freed in cleanup_graphics() — otherwise the dangling pointer survives a
// SCREEN→GFX.CLOSE→SCREEN cycle and crashes the second run.
static SDL_Texture*       g_plot_tex   = nullptr;
static int                g_plot_tex_w = 0;
static int                g_plot_tex_h = 0;
static std::vector<uint32_t> g_plot_buf;

// Reusable scratch buffer for GFX.PLOT_POINTS. SDL3's renderer backends
// don't all copy the SDL_FPoint array on submit — a per-frame local
// vector would be freed before the GPU consumed it. File-scope keeps the
// pointer valid until the next call (which is fine: the renderer drains
// its queue before the next PLOT_POINTS arrives, on RenderPresent).
static std::vector<SDL_FPoint> g_pts_buf;
static bool          g_sdl_init = false;

// Key buffer for INKEY$ in GFX mode
static std::string g_last_key;
static bool g_key_available = false;

// Shared SDL event queue: SCREENFLIP (and gfx_pump_events) push events here.
// event_poll() in vm.cpp drains it to dispatch to ON handlers, avoiding the
// race where SCREENFLIP's SDL_PollEvent eats events before event_poll sees them.
static std::vector<SDL_Event> g_pending_sdl_events;

void gfx_push_event(const SDL_Event& ev) { g_pending_sdl_events.push_back(ev); }
bool gfx_has_pending_events() { return !g_pending_sdl_events.empty(); }
std::vector<SDL_Event> gfx_drain_pending_events() {
    std::vector<SDL_Event> out;
    out.swap(g_pending_sdl_events);
    return out;
}

// REPL workspace-switch hook (Ctrl+F1..F4). Null in standalone mode.
static bool g_repl_active = false;
static void (*g_repl_switch_cb)(int) = nullptr;
void gfx_set_repl_switch_hook(bool active, void (*cb)(int)) {
    g_repl_active = active;
    g_repl_switch_cb = cb;
}
// Returns true (and dispatches) if `ev` is the Ctrl+F1..F4 chord while the
// REPL hook is active; the caller should `continue` so ImGui and ON-handlers
// never see the event.
static bool gfx_intercept_repl_chord(const SDL_Event& ev) {
    if (!g_repl_active || !g_repl_switch_cb) return false;
    if (ev.type != SDL_EVENT_KEY_DOWN || ev.key.repeat) return false;
    if (!(ev.key.mod & SDL_KMOD_CTRL)) return false;
    SDL_Keycode k = ev.key.key;
    if (k < SDLK_F1 || k > SDLK_F4) return false;
    g_repl_switch_cb((int)(k - SDLK_F1));
    return true;
}
static int           g_screen_w = 0;
static int           g_screen_h = 0;
static float         g_scale    = 1.0f;

// Current draw color
static Uint8 g_draw_r = 255, g_draw_g = 255, g_draw_b = 255, g_draw_a = 255;

// Current font
static TTF_Font* g_font = nullptr;
static std::string g_font_path;
static float g_font_size = 16.0f;
static bool g_ttf_init = false;

// Audio state
static bool g_audio_init = false;
static MIX_Mixer* g_mixer = nullptr;

// Image cache: id -> texture
static int g_next_image_id = 1;
static std::unordered_map<int, SDL_Texture*> g_images;

// ── Sprite state ────────────────────────────────────────────────
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

static std::map<int, Sprite> g_sprites;
static int g_next_sprite_id = 1;
static Uint64 g_last_update_tick = 0;

// ── Tilemap state ───────────────────────────────────────────────
struct Tilemap {
    int tileset_id;           // references g_images (spritesheet)
    int tile_w, tile_h;       // size of each tile in the tileset
    int tileset_cols;         // columns in the tileset image
    std::vector<std::vector<int>> data; // 2D grid of tile IDs (0 = empty)
    int rows, cols;           // map dimensions
};

static std::unordered_map<std::string, Tilemap> g_tilemaps;

// ── Camera state ────────────────────────────────────────────────
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
static Camera g_cam;

// ── Particle state ──────────────────────────────────────────────
struct Particle {
    float x, y, vx, vy;
    float life, max_life;        // seconds
    Uint8 r, g, b;
    float size;
    float gravity;
};
static std::vector<Particle> g_particles;

// ── Helpers ─────────────────────────────────────────────────────

static void ensure_screen(const char* fn) {
    if (!g_renderer)
        throw jdError(ErrCode::RUNTIME_ERROR, std::string(fn) + ": no screen (call SCREEN first)");
}

static void apply_draw_color() {
    SDL_SetRenderDrawColor(g_renderer, g_draw_r, g_draw_g, g_draw_b, g_draw_a);
}

static Sprite& get_sprite(const char* fn, int id) {
    auto it = g_sprites.find(id);
    if (it == g_sprites.end())
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": invalid sprite id " + std::to_string(id));
    return it->second;
}

static void draw_one_sprite(const Sprite& sp) {
    auto it = g_images.find(sp.texture_id);
    if (it == g_images.end()) return;
    SDL_Texture* tex = it->second;
    SDL_SetTextureAlphaMod(tex, (Uint8)sp.alpha);

    // Source rect: full texture or spritesheet frame
    SDL_FRect* src_ptr = nullptr;
    SDL_FRect src;
    if (sp.frame_w > 0 && sp.frame_h > 0) {
        int col = sp.current_frame % sp.cols;
        int row = sp.current_frame / sp.cols;
        src = { (float)(col * sp.frame_w), (float)(row * sp.frame_h),
                (float)sp.frame_w, (float)sp.frame_h };
        src_ptr = &src;
    }

    SDL_FRect dst = { sp.x, sp.y, sp.w * sp.scale_x, sp.h * sp.scale_y };
    SDL_FlipMode flip = SDL_FLIP_NONE;
    if (sp.flip_h && sp.flip_v) flip = (SDL_FlipMode)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL);
    else if (sp.flip_h) flip = SDL_FLIP_HORIZONTAL;
    else if (sp.flip_v) flip = SDL_FLIP_VERTICAL;
    SDL_FPoint center = { sp.origin_x * sp.scale_x, sp.origin_y * sp.scale_y };
    SDL_RenderTextureRotated(g_renderer, tex, src_ptr, &dst, sp.angle, &center, flip);
}

// Extract optional RGB from args starting at index `off`; returns true if found
static bool extract_rgb(const std::vector<Value>& args, size_t off,
                        Uint8& r, Uint8& g, Uint8& b) {
    if (off + 2 < args.size()) {
        r = (Uint8)args[off].to_int();
        g = (Uint8)args[off + 1].to_int();
        b = (Uint8)args[off + 2].to_int();
        return true;
    }
    return false;
}

// Set color temporarily, restoring after scope
struct ColorGuard {
    Uint8 r, g, b, a;
    bool active;
    ColorGuard(bool has_color, Uint8 nr, Uint8 ng, Uint8 nb)
        : r(g_draw_r), g(g_draw_g), b(g_draw_b), a(g_draw_a), active(has_color) {
        if (active) SDL_SetRenderDrawColor(g_renderer, nr, ng, nb, 255);
    }
    ~ColorGuard() {
        if (active) SDL_SetRenderDrawColor(g_renderer, r, g, b, a);
    }
};

// ── Circle drawing (midpoint algorithm) ─────────────────────────

static void draw_circle_outline(float cx, float cy, float radius) {
    int r = (int)radius;
    int x = r, y = 0, d = 1 - r;
    while (x >= y) {
        SDL_RenderPoint(g_renderer, cx + x, cy + y);
        SDL_RenderPoint(g_renderer, cx - x, cy + y);
        SDL_RenderPoint(g_renderer, cx + x, cy - y);
        SDL_RenderPoint(g_renderer, cx - x, cy - y);
        SDL_RenderPoint(g_renderer, cx + y, cy + x);
        SDL_RenderPoint(g_renderer, cx - y, cy + x);
        SDL_RenderPoint(g_renderer, cx + y, cy - x);
        SDL_RenderPoint(g_renderer, cx - y, cy - x);
        y++;
        if (d < 0) { d += 2 * y + 1; }
        else { x--; d += 2 * (y - x) + 1; }
    }
}

static void draw_circle_filled(float cx, float cy, float radius) {
    int r = (int)radius;
    int x = r, y = 0, d = 1 - r;
    while (x >= y) {
        SDL_RenderLine(g_renderer, cx - x, cy + y, cx + x, cy + y);
        SDL_RenderLine(g_renderer, cx - x, cy - y, cx + x, cy - y);
        SDL_RenderLine(g_renderer, cx - y, cy + x, cx + y, cy + x);
        SDL_RenderLine(g_renderer, cx - y, cy - x, cx + y, cy - x);
        y++;
        if (d < 0) { d += 2 * y + 1; }
        else { x--; d += 2 * (y - x) + 1; }
    }
}

// ── Ellipse drawing ─────────────────────────────────────────────

static void draw_ellipse_outline(float cx, float cy, float rx, float ry) {
    // Bresenham-style ellipse
    float rx2 = rx * rx, ry2 = ry * ry;
    float x = 0, y = ry;
    float px = 0, py = 2 * rx2 * y;

    // Region 1
    float d1 = ry2 - rx2 * ry + 0.25f * rx2;
    while (px < py) {
        SDL_RenderPoint(g_renderer, cx + x, cy + y);
        SDL_RenderPoint(g_renderer, cx - x, cy + y);
        SDL_RenderPoint(g_renderer, cx + x, cy - y);
        SDL_RenderPoint(g_renderer, cx - x, cy - y);
        x++; px += 2 * ry2;
        if (d1 < 0) { d1 += ry2 + px; }
        else { y--; py -= 2 * rx2; d1 += ry2 + px - py; }
    }

    // Region 2
    float d2 = ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        SDL_RenderPoint(g_renderer, cx + x, cy + y);
        SDL_RenderPoint(g_renderer, cx - x, cy + y);
        SDL_RenderPoint(g_renderer, cx + x, cy - y);
        SDL_RenderPoint(g_renderer, cx - x, cy - y);
        y--; py -= 2 * rx2;
        if (d2 > 0) { d2 += rx2 - py; }
        else { x++; px += 2 * ry2; d2 += rx2 - py + px; }
    }
}

static void draw_ellipse_filled(float cx, float cy, float rx, float ry) {
    for (int y = (int)-ry; y <= (int)ry; y++) {
        float halfW = rx * std::sqrt(1.0f - (float)(y * y) / (ry * ry));
        SDL_RenderLine(g_renderer, cx - halfW, cy + y, cx + halfW, cy + y);
    }
}

// ── Rounded rect ────────────────────────────────────────────────

static void draw_rounded_rect_outline(float x, float y, float w, float h, float r) {
    // Clamp radius
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    // Straight edges
    SDL_RenderLine(g_renderer, x + r, y, x + w - r, y);           // top
    SDL_RenderLine(g_renderer, x + r, y + h, x + w - r, y + h);   // bottom
    SDL_RenderLine(g_renderer, x, y + r, x, y + h - r);           // left
    SDL_RenderLine(g_renderer, x + w, y + r, x + w, y + h - r);   // right
    // Corner arcs (quarter circles)
    int ir = (int)r, ix = ir, iy = 0, d = 1 - ir;
    while (ix >= iy) {
        // Top-right
        SDL_RenderPoint(g_renderer, x + w - r + ix, y + r - iy);
        SDL_RenderPoint(g_renderer, x + w - r + iy, y + r - ix);
        // Top-left
        SDL_RenderPoint(g_renderer, x + r - ix, y + r - iy);
        SDL_RenderPoint(g_renderer, x + r - iy, y + r - ix);
        // Bottom-right
        SDL_RenderPoint(g_renderer, x + w - r + ix, y + h - r + iy);
        SDL_RenderPoint(g_renderer, x + w - r + iy, y + h - r + ix);
        // Bottom-left
        SDL_RenderPoint(g_renderer, x + r - ix, y + h - r + iy);
        SDL_RenderPoint(g_renderer, x + r - iy, y + h - r + ix);
        iy++;
        if (d < 0) { d += 2 * iy + 1; }
        else { ix--; d += 2 * (iy - ix) + 1; }
    }
}

static void draw_rounded_rect_filled(float x, float y, float w, float h, float r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    // Center rect
    SDL_FRect center = {x, y + r, w, h - 2 * r};
    SDL_RenderFillRect(g_renderer, &center);
    // Top/bottom strips
    SDL_FRect top = {x + r, y, w - 2 * r, r};
    SDL_FRect bot = {x + r, y + h - r, w - 2 * r, r};
    SDL_RenderFillRect(g_renderer, &top);
    SDL_RenderFillRect(g_renderer, &bot);
    // Corner fills
    int ir = (int)r, ix = ir, iy = 0, d = 1 - ir;
    while (ix >= iy) {
        SDL_RenderLine(g_renderer, x + r - ix, y + r - iy, x + w - r + ix, y + r - iy);
        SDL_RenderLine(g_renderer, x + r - iy, y + r - ix, x + w - r + iy, y + r - ix);
        SDL_RenderLine(g_renderer, x + r - ix, y + h - r + iy, x + w - r + ix, y + h - r + iy);
        SDL_RenderLine(g_renderer, x + r - iy, y + h - r + ix, x + w - r + iy, y + h - r + ix);
        iy++;
        if (d < 0) { d += 2 * iy + 1; }
        else { ix--; d += 2 * (iy - ix) + 1; }
    }
}

// ── Circle sector ───────────────────────────────────────────────

static void draw_sector_outline(float cx, float cy, float radius, float startAngle, float endAngle) {
    float a = startAngle * (float)M_PI / 180.0f;
    float b = endAngle * (float)M_PI / 180.0f;
    int steps = std::max(16, (int)(radius * std::abs(b - a) / (2 * M_PI) * 64));
    float step = (b - a) / steps;
    // Draw from center to arc start
    SDL_RenderLine(g_renderer, cx, cy, cx + radius * std::cos(a), cy + radius * std::sin(a));
    // Draw arc
    for (int i = 0; i < steps; i++) {
        float a1 = a + i * step, a2 = a + (i + 1) * step;
        SDL_RenderLine(g_renderer,
            cx + radius * std::cos(a1), cy + radius * std::sin(a1),
            cx + radius * std::cos(a2), cy + radius * std::sin(a2));
    }
    // Draw from arc end back to center
    SDL_RenderLine(g_renderer, cx + radius * std::cos(b), cy + radius * std::sin(b), cx, cy);
}

static void draw_sector_filled(float cx, float cy, float radius, float startAngle, float endAngle) {
    float a = startAngle * (float)M_PI / 180.0f;
    float b = endAngle * (float)M_PI / 180.0f;
    int steps = std::max(32, (int)(radius * std::abs(b - a) / (2 * M_PI) * 64));
    float step_angle = (b - a) / steps;

    // Use SDL_RenderGeometry for triangle fan fill
    // Build vertices: center + arc points
    std::vector<SDL_Vertex> verts;
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(g_renderer, &cr, &cg, &cb, &ca);
    SDL_FColor fc = {cr / 255.0f, cg / 255.0f, cb / 255.0f, ca / 255.0f};

    SDL_Vertex center_v;
    center_v.position = {cx, cy};
    center_v.color = fc;

    for (int i = 0; i < steps; i++) {
        float a1 = a + i * step_angle;
        float a2 = a + (i + 1) * step_angle;
        SDL_Vertex v1, v2;
        v1.position = {cx + radius * std::cos(a1), cy + radius * std::sin(a1)};
        v1.color = fc;
        v2.position = {cx + radius * std::cos(a2), cy + radius * std::sin(a2)};
        v2.color = fc;
        verts.push_back(center_v);
        verts.push_back(v1);
        verts.push_back(v2);
    }

    if (!verts.empty()) {
        SDL_RenderGeometry(g_renderer, nullptr, verts.data(), (int)verts.size(), nullptr, 0);
    }
}

// ── Matrix-based drawing helpers ────────────────────────────────

static bool is_matrix(const Value& v) {
    return v.type == ValueType::ARRAY && !v.as_array()->elements.empty()
        && v.as_array()->elements[0].type == ValueType::ARRAY;
}

static bool is_color_array(const Value& v) {
    // Color array: [[r,g,b], [r,g,b], ...] or single [r,g,b]
    return v.type == ValueType::ARRAY;
}

// Get color from a colors array at index i (cycling)
static void get_color_at(const Value& colors, size_t i, Uint8& r, Uint8& g, Uint8& b) {
    auto* arr = colors.as_array();
    if (arr->elements.empty()) return;
    auto& c = arr->elements[i % arr->elements.size()];
    if (c.type == ValueType::ARRAY) {
        auto* ca = c.as_array();
        if (ca->elements.size() >= 3) {
            r = (Uint8)ca->elements[0].to_int();
            g = (Uint8)ca->elements[1].to_int();
            b = (Uint8)ca->elements[2].to_int();
        }
    }
}

// ── Public accessors for CLS integration ────────────────────────

bool gfx_is_active() { return g_renderer != nullptr; }

void gfx_clear(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_renderer) return;
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_RenderClear(g_renderer);
    // Restore draw color
    SDL_SetRenderDrawColor(g_renderer, g_draw_r, g_draw_g, g_draw_b, g_draw_a);
}

// ── Cleanup (forward declared for gfx_shutdown) ─────────────────
static void cleanup_graphics();

// ── Public shutdown ─────────────────────────────────────────────

void gfx_shutdown() { cleanup_graphics(); }

bool gfx_has_key() { return g_key_available; }
std::string gfx_get_key() {
    if (!g_key_available) return "";
    g_key_available = false;
    return g_last_key;
}

// Pump pending SDL events without flipping the back buffer. Used by WAITKEY$
// (and any other native that needs to wait for keys without calling
// SCREENFLIP) so the window stays responsive and key events are buffered
// into g_key_available / g_last_key.
void gfx_pump_events() {
    if (!g_renderer) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (gfx_intercept_repl_chord(ev)) continue;
#ifdef IMGUI
        gui_process_event(&ev);
#endif
        if (ev.type == SDL_EVENT_QUIT) {
            g_last_key = std::string(1, (char)27);
            g_key_available = true;
            gfx_push_event(ev);
            continue;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            SDL_Keycode key = ev.key.key;
            if (key == SDLK_ESCAPE) {
                g_last_key = std::string(1, (char)27);
            } else if (key == SDLK_RETURN) {
                g_last_key = std::string(1, (char)13);
            } else if (key == SDLK_BACKSPACE) {
                g_last_key = std::string(1, (char)8);
            } else if (key == SDLK_TAB) {
                g_last_key = std::string(1, (char)9);
            } else {
                const char* name = SDL_GetKeyName(key);
                if (name && name[0] && !name[1]) {
                    g_last_key = std::string(1, name[0]);
                } else if (name) {
                    g_last_key = name;
                }
            }
            g_key_available = true;
        }
        // Queue for ON handlers
        gfx_push_event(ev);
    }
}

// ── Cleanup ─────────────────────────────────────────────────────

static void cleanup_graphics() {
    // Free cached images
    for (auto& [id, tex] : g_images) {
        if (tex) SDL_DestroyTexture(tex);
    }
    g_images.clear();
    g_sprites.clear();
    g_next_sprite_id = 1;
    g_tilemaps.clear();
    g_tiled.shutdown();
    g_cam = Camera{};
    g_particles.clear();

    if (g_font) { TTF_CloseFont(g_font); g_font = nullptr; }
    if (g_ttf_init) { TTF_Quit(); g_ttf_init = false; }

    if (g_audio_init) {
        if (g_mixer) { MIX_DestroyMixer(g_mixer); g_mixer = nullptr; }
        MIX_Quit();
        g_audio_init = false;
    }

#ifdef IMGUI
    gui_shutdown();
#endif

    if (g_plot_tex) { SDL_DestroyTexture(g_plot_tex); g_plot_tex = nullptr; }
    g_plot_tex_w = 0;
    g_plot_tex_h = 0;
    g_plot_buf.clear();
    g_plot_buf.shrink_to_fit();

    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }
    if (g_sdl_init) { SDL_Quit(); g_sdl_init = false; }
}

// ── Register all graphics/audio builtins ────────────────────────

void register_graphics_builtins(VM& vm) {

    // ── SCREEN width, height, [title$], [scalefactor] ───────────

    vm.register_native("SCREEN", 2, 4, [](const std::vector<Value>& args) -> Value {
        int w = (int)args[0].to_int();
        int h = (int)args[1].to_int();
        std::string title = (args.size() >= 3 && args[2].type == ValueType::STRING)
            ? args[2].as_string()->data : "jdBasic";
        float scale = (args.size() >= 4) ? (float)args[3].to_double() : 1.0f;

        // Clean up previous window if any
        if (g_plot_tex) { SDL_DestroyTexture(g_plot_tex); g_plot_tex = nullptr; }
        g_plot_tex_w = 0;
        g_plot_tex_h = 0;
#ifdef IMGUI
        // Tear ImGui down before destroying the renderer it was bound to.
        // gui_init early-returns when already initialised, so without
        // this the second SCREEN keeps gui.cpp's stale g_renderer
        // pointer and gui_new_frame re-applies logical presentation
        // to the freed renderer — the new one ends up with
        // PRESENTATION_DISABLED and draws 1:1 in the top-left.
        gui_shutdown();
#endif
        if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
        if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }

        if (!g_sdl_init) {
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("SDL_Init failed: ") + SDL_GetError());
            }
            g_sdl_init = true;
        }

        g_screen_w = w;
        g_screen_h = h;
        g_scale = scale;

        int win_w = (int)(w * scale);
        int win_h = (int)(h * scale);

        // Clamp to display size to avoid unintended fullscreen
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(display);
        if (dm) {
            if (win_w > dm->w) win_w = dm->w - 40;
            if (win_h > dm->h) win_h = dm->h - 80;
        }

        g_window = SDL_CreateWindow(title.c_str(), win_w, win_h, SDL_WINDOW_RESIZABLE);
        if (!g_window)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("SDL_CreateWindow failed: ") + SDL_GetError());

        g_renderer = SDL_CreateRenderer(g_window, nullptr);
        if (!g_renderer)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("SDL_CreateRenderer failed: ") + SDL_GetError());

        // Set logical size for scaling
        SDL_SetRenderLogicalPresentation(g_renderer, w, h,
            SDL_LOGICAL_PRESENTATION_LETTERBOX);

        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);
        SDL_RenderPresent(g_renderer);

        // Bring window to front and give it focus
        SDL_RaiseWindow(g_window);

#ifdef IMGUI
        gui_init(g_window, g_renderer, g_scale);
#endif

        // Init TiledMap system with the renderer
        g_tiled.init(g_renderer);

        // Register cleanup at exit
        static bool atexit_set = false;
        if (!atexit_set) { std::atexit(cleanup_graphics); atexit_set = true; }

        return Value::make_none();
    });

    // ── SCREENFLIP ──────────────────────────────────────────────

    // ── SCREENWIDTH() / SCREENHEIGHT() — query the logical SCREEN size ──
    // Returns 0 if SCREEN was never called, so user code can detect that.
    vm.register_native("SCREENWIDTH", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_i64(g_screen_w);
    });
    vm.register_native("SCREENHEIGHT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_i64(g_screen_h);
    });

    vm.register_native("SCREENFLIP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_screen("SCREENFLIP");

#ifdef IMGUI
        gui_render(g_renderer);
#endif
        SDL_RenderPresent(g_renderer);
#ifdef IMGUI
        gui_new_frame();
#endif

        // Process events to keep window responsive. Also push all events
        // to the shared queue so event_poll() in vm.cpp can dispatch them
        // to ON handlers without a second SDL_PollEvent that would always
        // see an empty queue (because we already drained it here).
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (gfx_intercept_repl_chord(ev)) continue;
#ifdef IMGUI
            gui_process_event(&ev);
#endif
            if (ev.type == SDL_EVENT_QUIT) {
                gfx_push_event(ev); // also let event_poll dispatch ON QUIT
                cleanup_graphics();
                return Value::make_none();
            }
            // Buffer key-down events for INKEY$ in GFX mode
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                SDL_Keycode key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    g_last_key = std::string(1, (char)27);
                } else if (key == SDLK_RETURN) {
                    g_last_key = std::string(1, (char)13);
                } else if (key == SDLK_BACKSPACE) {
                    g_last_key = std::string(1, (char)8);
                } else if (key == SDLK_TAB) {
                    g_last_key = std::string(1, (char)9);
                } else {
                    const char* name = SDL_GetKeyName(key);
                    if (name && name[0] && !name[1]) {
                        g_last_key = std::string(1, name[0]);
                    } else if (name) {
                        g_last_key = name;
                    }
                }
                g_key_available = true;
            }
            // Queue ALL events for ON handlers
            gfx_push_event(ev);
        }
        return Value::make_none();
    });

    // ── DRAWCOLOR r, g, b ───────────────────────────────────────

    vm.register_native("DRAWCOLOR", 3, 4, [](const std::vector<Value>& args) -> Value {
        g_draw_r = (Uint8)args[0].to_int();
        g_draw_g = (Uint8)args[1].to_int();
        g_draw_b = (Uint8)args[2].to_int();
        g_draw_a = (args.size() >= 4) ? (Uint8)args[3].to_int() : 255;
        if (g_renderer) apply_draw_color();
        return Value::make_none();
    });

    // ── SETFONT filename$, size ─────────────────────────────────

    vm.register_native("SETFONT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string path = resolve_asset_path(args[0].as_string()->data);
        float size = (float)args[1].to_double();

        if (!g_ttf_init) {
            if (!TTF_Init())
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("TTF_Init failed: ") + SDL_GetError());
            g_ttf_init = true;
        }

        if (g_font) { TTF_CloseFont(g_font); g_font = nullptr; }

        g_font = TTF_OpenFont(path.c_str(), size);
        if (!g_font)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("TTF_OpenFont failed: ") + SDL_GetError());

        g_font_path = path;
        g_font_size = size;
        return Value::make_none();
    });

    // ── PSET x, y, [r, g, b] OR PSET matrix, [colors] ─────────

    vm.register_native("PSET", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("PSET");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            // Matrix mode: each row is [x, y]
            auto* rows = args[0].as_array();
            bool hasColors = (args.size() >= 2 && args[1].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* pt = rows->elements[i].as_array();
                if (pt->elements.size() >= 2) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[1], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    SDL_RenderPoint(g_renderer, (float)pt->elements[0].to_double(),
                                               (float)pt->elements[1].to_double());
                }
            }
            apply_draw_color();
        } else {
            // Scalar: PSET x, y, [r, g, b]
            float x = (float)args[0].to_double();
            float y = (float)args[1].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 2, r, g, b);
            ColorGuard cg(has, r, g, b);
            SDL_RenderPoint(g_renderer, x, y);
        }
        return Value::make_none();
    });

    // ── GFX.PLOT_POINTS xs, ys, [rgb] ─────────────────────────
    // Fast batch pixel plot. Takes parallel flat arrays:
    //   xs   : 1-D array of x coords (number per pixel)
    //   ys   : 1-D array of y coords
    //   rgb  : optional flat array of length N*3 [r0,g0,b0, r1,g1,b1, ...]
    //          OR a single colour [r,g,b] applied to all points
    //          OR omitted → uses current DRAWCOLOR
    // Uses SDL_RenderPoints when all pixels share a colour (one draw call),
    // otherwise issues per-pixel SetDrawColor + RenderPoint internally.
    vm.register_native("GFX.PLOT_POINTS", 2, 3, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.PLOT_POINTS");
        if (args[0].type != ValueType::ARRAY || args[1].type != ValueType::ARRAY)
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.PLOT_POINTS: xs and ys must be arrays");

        auto& xs = args[0].as_array()->elements;
        auto& ys = args[1].as_array()->elements;
        size_t n = std::min(xs.size(), ys.size());
        if (n == 0) return Value::make_none();

        // ── Path A: no colour (use current draw colour) ──
        // ── Path B: single [r,g,b] colour for all points ──
        // ── Path C: flat per-pixel rgb stream of length N*3 ──
        bool has_rgb_arg = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
        bool flat_per_pixel = false;
        bool single_colour  = false;
        Uint8 sr = g_draw_r, sg = g_draw_g, sb = g_draw_b;

        if (has_rgb_arg) {
            auto& cs = args[2].as_array()->elements;
            if (cs.size() == 3 && cs[0].type != ValueType::ARRAY) {
                single_colour = true;
                sr = (Uint8)cs[0].to_int();
                sg = (Uint8)cs[1].to_int();
                sb = (Uint8)cs[2].to_int();
            } else if (cs.size() >= n * 3) {
                flat_per_pixel = true;
            }
        }

        // Fill the file-scope scratch buffer. Reusing it across calls
        // keeps the SDL_FPoint pointer valid even if SDL3's renderer
        // defers consumption until SDL_RenderPresent.
        g_pts_buf.resize(n);
        for (size_t i = 0; i < n; i++) {
            g_pts_buf[i].x = (float)xs[i].to_double();
            g_pts_buf[i].y = (float)ys[i].to_double();
        }

        if (!has_rgb_arg) {
            SDL_RenderPoints(g_renderer, g_pts_buf.data(), (int)n);
        } else if (single_colour) {
            SDL_SetRenderDrawColor(g_renderer, sr, sg, sb, 255);
            SDL_RenderPoints(g_renderer, g_pts_buf.data(), (int)n);
            apply_draw_color();
        } else if (flat_per_pixel) {
            auto& cs = args[2].as_array()->elements;
            for (size_t i = 0; i < n; i++) {
                SDL_SetRenderDrawColor(g_renderer,
                    (Uint8)cs[i*3 + 0].to_int(),
                    (Uint8)cs[i*3 + 1].to_int(),
                    (Uint8)cs[i*3 + 2].to_int(), 255);
                SDL_RenderPoint(g_renderer, g_pts_buf[i].x, g_pts_buf[i].y);
            }
            apply_draw_color();
        } else {
            // Fallback: use Matrix-of-rows format like PSET
            for (size_t i = 0; i < n; i++) {
                Uint8 r, g, b;
                get_color_at(args[2], i, r, g, b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderPoint(g_renderer, g_pts_buf[i].x, g_pts_buf[i].y);
            }
            apply_draw_color();
        }
        return Value::make_none();
    });

    // ── GFX.PLOT_POINTS_TEX(xs, ys, rgb_lut, [bg_rgb]) ───────
    // The fastest way to plot many coloured pixels: scatter into a
    // streaming texture once per frame and upload to the GPU in one call.
    //
    //   xs, ys   : 1-D arrays of pixel coordinates (length N)
    //   rgb_lut  : flat array of length N*3 [r0,g0,b0, r1,g1,b1, ...]
    //   bg_rgb   : optional [r,g,b] background fill (default black)
    //
    // Replaces 70k SDL_RenderPoint calls with a single SDL_UpdateTexture
    // + SDL_RenderTexture pair. The persistent texture lives at file scope
    // (see top of this file) so it can be freed by cleanup_graphics().
    {
        vm.register_native("GFX.PLOT_POINTS_TEX", 3, 4, [](const std::vector<Value>& args) -> Value {
            ensure_screen("GFX.PLOT_POINTS_TEX");
            // Use the LOGICAL size (the SCREEN dimensions). The BASIC code
            // computes pixel coordinates in this space, so the texture must
            // match it — SDL_SetRenderLogicalPresentation handles the upscale
            // to the actual window/output. Using the physical output size
            // here would put all points in a top-left sub-rectangle.
            int w = g_screen_w;
            int h = g_screen_h;
            if (w <= 0 || h <= 0) return Value::make_none();

            if (g_plot_tex == nullptr || g_plot_tex_w != w || g_plot_tex_h != h) {
                if (g_plot_tex) SDL_DestroyTexture(g_plot_tex);
                g_plot_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888,
                                                SDL_TEXTUREACCESS_STREAMING, w, h);
                if (!g_plot_tex)
                    throw jdError(ErrCode::RUNTIME_ERROR,
                        std::string("PLOT_POINTS_TEX: SDL_CreateTexture failed: ") + SDL_GetError());
                g_plot_tex_w = w;
                g_plot_tex_h = h;
                g_plot_buf.assign((size_t)w * h, 0xFF000000u);
            }

            // Background fill (default black)
            uint32_t bg = 0xFF000000u;
            if (args.size() >= 4 && args[3].type == ValueType::ARRAY) {
                auto& cs = args[3].as_array()->elements;
                if (cs.size() >= 3) {
                    uint32_t r = (uint32_t)(cs[0].to_int() & 0xFF);
                    uint32_t g = (uint32_t)(cs[1].to_int() & 0xFF);
                    uint32_t b = (uint32_t)(cs[2].to_int() & 0xFF);
                    bg = 0xFF000000u | (b << 16) | (g << 8) | r;
                }
            }
            std::fill(g_plot_buf.begin(), g_plot_buf.end(), bg);

            if (args[0].type != ValueType::ARRAY || args[1].type != ValueType::ARRAY ||
                args[2].type != ValueType::ARRAY)
                throw jdError(ErrCode::RUNTIME_ERROR, "PLOT_POINTS_TEX: xs/ys/rgb must be arrays");

            auto& xs = args[0].as_array()->elements;
            auto& ys = args[1].as_array()->elements;
            auto& rgb = args[2].as_array()->elements;
            size_t n = std::min(xs.size(), ys.size());
            // Scatter pixels into the buffer
            for (size_t i = 0; i < n; i++) {
                int px = (int)xs[i].to_double();
                int py = (int)ys[i].to_double();
                if ((unsigned)px >= (unsigned)w || (unsigned)py >= (unsigned)h) continue;
                size_t base = i * 3;
                if (base + 2 >= rgb.size()) break;
                uint32_t r = (uint32_t)(rgb[base + 0].to_int() & 0xFF);
                uint32_t g = (uint32_t)(rgb[base + 1].to_int() & 0xFF);
                uint32_t b = (uint32_t)(rgb[base + 2].to_int() & 0xFF);
                g_plot_buf[(size_t)py * (size_t)w + (size_t)px] =
                    0xFF000000u | (b << 16) | (g << 8) | r;
            }

            SDL_UpdateTexture(g_plot_tex, nullptr, g_plot_buf.data(), w * 4);
            SDL_RenderTexture(g_renderer, g_plot_tex, nullptr, nullptr);
            return Value::make_none();
        });
    }

    // ── GFX.HSV_RGB(h, s, v) -> [r,g,b] ───────────────────────
    // Fast HSV → RGB conversion in C++. Avoids the per-pixel cost
    // of a user-defined HSVtoRGB function.
    vm.register_native("GFX.HSV_RGB", 3, 3, [](const std::vector<Value>& args) -> Value {
        double h = args[0].to_double();
        double s = args[1].to_double();
        double v = args[2].to_double();
        h = h - 360.0 * std::floor(h / 360.0);
        if (s < 0) s = 0; else if (s > 1) s = 1;
        if (v < 0) v = 0; else if (v > 1) v = 1;
        double c = v * s;
        double hh = h / 60.0;
        double xv = c * (1.0 - std::fabs(std::fmod(hh, 2.0) - 1.0));
        double m = v - c;
        double rp, gp, bp;
        if      (hh < 1) { rp = c; gp = xv; bp = 0;  }
        else if (hh < 2) { rp = xv; gp = c; bp = 0;  }
        else if (hh < 3) { rp = 0;  gp = c; bp = xv; }
        else if (hh < 4) { rp = 0;  gp = xv; bp = c; }
        else if (hh < 5) { rp = xv; gp = 0; bp = c;  }
        else             { rp = c;  gp = 0; bp = xv; }
        Value r = Value::make_array();
        auto* a = r.as_array();
        a->elements.push_back(Value::make_i64((int64_t)((rp + m) * 255.0)));
        a->elements.push_back(Value::make_i64((int64_t)((gp + m) * 255.0)));
        a->elements.push_back(Value::make_i64((int64_t)((bp + m) * 255.0)));
        return r;
    });

    // ── LINE x1, y1, x2, y2, [r, g, b] OR LINE matrix, [colors] ─

    vm.register_native("LINE", 1, 7, [](const std::vector<Value>& args) -> Value {
        ensure_screen("LINE");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool hasColors = (args.size() >= 2 && args[1].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* ln = rows->elements[i].as_array();
                if (ln->elements.size() >= 4) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[1], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    SDL_RenderLine(g_renderer,
                        (float)ln->elements[0].to_double(), (float)ln->elements[1].to_double(),
                        (float)ln->elements[2].to_double(), (float)ln->elements[3].to_double());
                }
            }
            apply_draw_color();
        } else {
            float x1 = (float)args[0].to_double(), y1 = (float)args[1].to_double();
            float x2 = (float)args[2].to_double(), y2 = (float)args[3].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 4, r, g, b);
            ColorGuard cg(has, r, g, b);
            SDL_RenderLine(g_renderer, x1, y1, x2, y2);
        }
        return Value::make_none();
    });

    // ── RECT x, y, w, h, [fill], [r, g, b] OR RECT matrix, [fill], [colors] ─

    vm.register_native("RECT", 1, 8, [](const std::vector<Value>& args) -> Value {
        ensure_screen("RECT");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool fill = (args.size() >= 2) ? args[1].to_bool() : false;
            bool hasColors = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* rc = rows->elements[i].as_array();
                if (rc->elements.size() >= 4) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[2], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    SDL_FRect rect = {(float)rc->elements[0].to_double(),
                                      (float)rc->elements[1].to_double(),
                                      (float)rc->elements[2].to_double(),
                                      (float)rc->elements[3].to_double()};
                    if (fill) SDL_RenderFillRect(g_renderer, &rect);
                    else SDL_RenderRect(g_renderer, &rect);
                }
            }
            apply_draw_color();
        } else {
            // RECT x, y, w, h, [r, g, b], [fill]
            float x = (float)args[0].to_double(), y = (float)args[1].to_double();
            float w = (float)args[2].to_double(), h = (float)args[3].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 4, r, g, b);
            ColorGuard cg(has, r, g, b);
            // fill is the last arg (after optional RGB)
            bool fill = false;
            if (has && args.size() >= 8) fill = args[7].to_bool();
            else if (!has && args.size() >= 5) fill = args[4].to_bool();
            SDL_FRect rect = {x, y, w, h};
            if (fill) SDL_RenderFillRect(g_renderer, &rect);
            else SDL_RenderRect(g_renderer, &rect);
        }
        return Value::make_none();
    });

    // ── CIRCLE cx, cy, r, [fill], [r, g, b] OR CIRCLE matrix, [fill], [colors] ─

    vm.register_native("CIRCLE", 1, 7, [](const std::vector<Value>& args) -> Value {
        ensure_screen("CIRCLE");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool fill = (args.size() >= 2) ? args[1].to_bool() : false;
            bool hasColors = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* c = rows->elements[i].as_array();
                if (c->elements.size() >= 3) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[2], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    float cx = (float)c->elements[0].to_double();
                    float cy = (float)c->elements[1].to_double();
                    float cr = (float)c->elements[2].to_double();
                    if (fill) draw_circle_filled(cx, cy, cr);
                    else draw_circle_outline(cx, cy, cr);
                }
            }
            apply_draw_color();
        } else {
            // CIRCLE cx, cy, r, [r, g, b], [fill]
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float cr = (float)args[2].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 3, r, g, b);
            ColorGuard cg(has, r, g, b);
            bool fill = false;
            if (has && args.size() >= 7) fill = args[6].to_bool();
            else if (!has && args.size() >= 4) fill = args[3].to_bool();
            if (fill) draw_circle_filled(cx, cy, cr);
            else draw_circle_outline(cx, cy, cr);
        }
        return Value::make_none();
    });

    // ── ELLIPSE cx, cy, rx, ry, [r, g, b], [fill] ──────────────

    vm.register_native("ELLIPSE", 1, 8, [](const std::vector<Value>& args) -> Value {
        ensure_screen("ELLIPSE");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool fill = (args.size() >= 2) ? args[1].to_bool() : false;
            bool hasColors = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* e = rows->elements[i].as_array();
                if (e->elements.size() >= 4) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[2], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    float cx = (float)e->elements[0].to_double();
                    float cy = (float)e->elements[1].to_double();
                    float rx = (float)e->elements[2].to_double();
                    float ry = (float)e->elements[3].to_double();
                    if (fill) draw_ellipse_filled(cx, cy, rx, ry);
                    else draw_ellipse_outline(cx, cy, rx, ry);
                }
            }
            apply_draw_color();
        } else {
            // ELLIPSE cx, cy, rx, ry, [r, g, b], [fill]
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float rx = (float)args[2].to_double();
            float ry = (float)args[3].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 4, r, g, b);
            ColorGuard cg(has, r, g, b);
            bool fill = false;
            if (has && args.size() >= 8) fill = args[7].to_bool();
            else if (!has && args.size() >= 5) fill = args[4].to_bool();
            if (fill) draw_ellipse_filled(cx, cy, rx, ry);
            else draw_ellipse_outline(cx, cy, rx, ry);
        }
        return Value::make_none();
    });

    // ── ROUNDED_RECT x, y, w, h, radius, [fill], [r, g, b] ────

    vm.register_native("ROUNDED_RECT", 1, 9, [](const std::vector<Value>& args) -> Value {
        ensure_screen("ROUNDED_RECT");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool fill = (args.size() >= 2) ? args[1].to_bool() : false;
            bool hasColors = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* rr = rows->elements[i].as_array();
                if (rr->elements.size() >= 5) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[2], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    float x = (float)rr->elements[0].to_double();
                    float y = (float)rr->elements[1].to_double();
                    float w = (float)rr->elements[2].to_double();
                    float h = (float)rr->elements[3].to_double();
                    float rad = (float)rr->elements[4].to_double();
                    if (fill) draw_rounded_rect_filled(x, y, w, h, rad);
                    else draw_rounded_rect_outline(x, y, w, h, rad);
                }
            }
            apply_draw_color();
        } else {
            // ROUNDED_RECT x, y, w, h, rad, [r, g, b], [fill]
            float x = (float)args[0].to_double();
            float y = (float)args[1].to_double();
            float w = (float)args[2].to_double();
            float h = (float)args[3].to_double();
            float rad = (float)args[4].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 5, r, g, b);
            ColorGuard cg(has, r, g, b);
            bool fill = false;
            if (has && args.size() >= 9) fill = args[8].to_bool();
            else if (!has && args.size() >= 6) fill = args[5].to_bool();
            if (fill) draw_rounded_rect_filled(x, y, w, h, rad);
            else draw_rounded_rect_outline(x, y, w, h, rad);
        }
        return Value::make_none();
    });

    // ── CIRCLE_SECTOR cx, cy, radius, start_angle, end_angle, [fill], [r, g, b] ─

    vm.register_native("CIRCLE_SECTOR", 1, 9, [](const std::vector<Value>& args) -> Value {
        ensure_screen("CIRCLE_SECTOR");

        if (args[0].type == ValueType::ARRAY && is_matrix(args[0])) {
            auto* rows = args[0].as_array();
            bool fill = (args.size() >= 2) ? args[1].to_bool() : false;
            bool hasColors = (args.size() >= 3 && args[2].type == ValueType::ARRAY);
            for (size_t i = 0; i < rows->elements.size(); i++) {
                auto* cs = rows->elements[i].as_array();
                if (cs->elements.size() >= 5) {
                    Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
                    if (hasColors) get_color_at(args[2], i, r, g, b);
                    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                    float cx = (float)cs->elements[0].to_double();
                    float cy = (float)cs->elements[1].to_double();
                    float rad = (float)cs->elements[2].to_double();
                    float sa = (float)cs->elements[3].to_double();
                    float ea = (float)cs->elements[4].to_double();
                    if (fill) draw_sector_filled(cx, cy, rad, sa, ea);
                    else draw_sector_outline(cx, cy, rad, sa, ea);
                }
            }
            apply_draw_color();
        } else {
            // CIRCLE_SECTOR cx, cy, radius, start_angle, end_angle, [r, g, b], [fill]
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float rad = (float)args[2].to_double();
            float sa = (float)args[3].to_double();
            float ea = (float)args[4].to_double();
            Uint8 r, g, b;
            bool has = extract_rgb(args, 5, r, g, b);
            ColorGuard cg(has, r, g, b);
            bool fill = false;
            if (has && args.size() >= 9) fill = args[8].to_bool();
            else if (!has && args.size() >= 6) fill = args[5].to_bool();
            if (fill) draw_sector_filled(cx, cy, rad, sa, ea);
            else draw_sector_outline(cx, cy, rad, sa, ea);
        }
        return Value::make_none();
    });

    // ── TEXT x, y, content$, [r, g, b] ──────────────────────────

    vm.register_native("TEXT", 3, 6, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TEXT");
        if (!g_font)
            throw jdError(ErrCode::RUNTIME_ERROR, "TEXT: no font loaded (call SETFONT first)");

        float x = (float)args[0].to_double();
        float y = (float)args[1].to_double();
        std::string text = args[2].to_string();
        if (text.empty()) return Value::make_none();

        Uint8 r = g_draw_r, g = g_draw_g, b = g_draw_b;
        extract_rgb(args, 3, r, g, b);

        SDL_Color color = {r, g, b, 255};
        SDL_Surface* surface = TTF_RenderText_Blended(g_font, text.c_str(), 0, color);
        if (!surface)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("TTF_RenderText failed: ") + SDL_GetError());

        SDL_Texture* texture = SDL_CreateTextureFromSurface(g_renderer, surface);
        float tw = (float)surface->w, th = (float)surface->h;
        SDL_DestroySurface(surface);

        if (texture) {
            SDL_FRect dst = {x, y, tw, th};
            SDL_RenderTexture(g_renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        return Value::make_none();
    });

    // ── PLOTRAW x, y, matrix, [scaleX, scaleY] ─────────────────

    vm.register_native("PLOTRAW", 3, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("PLOTRAW");

        float ox = (float)args[0].to_double();
        float oy = (float)args[1].to_double();
        auto* matrix = args[2].as_array();
        float sx = (args.size() >= 4) ? (float)args[3].to_double() : 1.0f;
        float sy = (args.size() >= 5) ? (float)args[4].to_double() : sx;

        int rows = (int)matrix->elements.size();
        if (rows == 0) return Value::make_none();

        for (int r = 0; r < rows; r++) {
            auto* row = matrix->elements[r].as_array();
            int cols = (int)row->elements.size();
            for (int c = 0; c < cols; c++) {
                auto& pixel = row->elements[c];
                if (pixel.type == ValueType::ARRAY) {
                    auto* rgba = pixel.as_array();
                    if (rgba->elements.size() >= 3) {
                        Uint8 pr = (Uint8)rgba->elements[0].to_int();
                        Uint8 pg = (Uint8)rgba->elements[1].to_int();
                        Uint8 pb = (Uint8)rgba->elements[2].to_int();
                        SDL_SetRenderDrawColor(g_renderer, pr, pg, pb, 255);
                        if (sx == 1.0f && sy == 1.0f) {
                            SDL_RenderPoint(g_renderer, ox + c, oy + r);
                        } else {
                            SDL_FRect dst = {ox + c * sx, oy + r * sy, sx, sy};
                            SDL_RenderFillRect(g_renderer, &dst);
                        }
                    }
                } else {
                    // Scalar: decode packed RGB integer (R*65536 + G*256 + B)
                    int64_t rgb = pixel.to_int();
                    if (rgb == 0) continue; // skip black/transparent pixels
                    Uint8 pr = (Uint8)((rgb >> 16) & 0xFF);
                    Uint8 pg = (Uint8)((rgb >> 8) & 0xFF);
                    Uint8 pb = (Uint8)(rgb & 0xFF);
                    SDL_SetRenderDrawColor(g_renderer, pr, pg, pb, 255);
                    if (sx == 1.0f && sy == 1.0f) {
                        SDL_RenderPoint(g_renderer, ox + c, oy + r);
                    } else {
                        SDL_FRect dst = {ox + c * sx, oy + r * sy, sx, sy};
                        SDL_RenderFillRect(g_renderer, &dst);
                    }
                }
            }
        }
        apply_draw_color();
        return Value::make_none();
    });

    // ── TOGGLE_FULLSCREEN ───────────────────────────────────────

    vm.register_native("TOGGLE_FULLSCREEN", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_screen("TOGGLE_FULLSCREEN");
        Uint32 flags = SDL_GetWindowFlags(g_window);
        bool want_fs = !(flags & SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowFullscreen(g_window, want_fs);
        // Wait for the window manager to actually apply the size change,
        // otherwise the renderer's output size is still stale when we
        // re-apply the logical presentation below.
        SDL_SyncWindow(g_window);
        // Re-apply logical presentation so the original SCREEN dimensions
        // are letterboxed into the new (fullscreen / windowed) output size.
        SDL_SetRenderLogicalPresentation(g_renderer, g_screen_w, g_screen_h,
            SDL_LOGICAL_PRESENTATION_LETTERBOX);
        return Value::make_none();
    });

    // ── Event/Input functions ───────────────────────────────────

    vm.register_native("GFX.POLLEVENT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!g_window) return Value::make_string("NONE");
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
#ifdef IMGUI
            if (gui_process_event(&ev)) {
                // Only swallow events when an ImGui widget is actually in
                // use — NOT just because ImGui-Nav has implicit keyboard
                // capture (which is on as soon as the window has focus).
                // Otherwise GFX-only programs lose every key. See
                // project_imgui_ate_keys.md.
                bool kb_active    = ImGui::IsAnyItemActive() || ImGui::IsAnyItemFocused();
                bool mouse_active = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
                bool is_key = (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP || ev.type == SDL_EVENT_TEXT_INPUT);
                bool is_mouse = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP || ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_WHEEL);
                if ((is_key && kb_active) || (is_mouse && mouse_active))
                    continue;
            }
#endif
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    return Value::make_string("QUIT");
                case SDL_EVENT_KEY_DOWN: {
                    const char* name = SDL_GetKeyName(ev.key.key);
                    return Value::make_string(std::string("KEYDOWN:") + (name ? name : "?"));
                }
                case SDL_EVENT_KEY_UP: {
                    const char* name = SDL_GetKeyName(ev.key.key);
                    return Value::make_string(std::string("KEYUP:") + (name ? name : "?"));
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    return Value::make_string("MOUSEDOWN:" + std::to_string(ev.button.button));
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    return Value::make_string("MOUSEUP:" + std::to_string(ev.button.button));
                case SDL_EVENT_MOUSE_MOTION:
                    return Value::make_string("MOUSEMOVE:" +
                        std::to_string((int)ev.motion.x) + "," + std::to_string((int)ev.motion.y));
                default: break;
            }
        }
        return Value::make_string("NONE");
    });

    vm.register_native("GFX.KEYSTATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        const bool* state = SDL_GetKeyboardState(nullptr);
        if (!state) return Value::make_bool(false);
        SDL_Scancode sc;
        if (args[0].type == ValueType::STRING) {
            sc = SDL_GetScancodeFromName(args[0].as_string()->data.c_str());
        } else {
            sc = (SDL_Scancode)args[0].to_int();
        }
        return Value::make_bool(state[sc]);
    });

    vm.register_native("GFX.MOUSEX", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float x, y;
        SDL_GetMouseState(&x, &y);
        return Value::make_i64((int64_t)x);
    });

    vm.register_native("GFX.MOUSEY", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float x, y;
        SDL_GetMouseState(&x, &y);
        return Value::make_i64((int64_t)y);
    });

    vm.register_native("GFX.MOUSEBUTTON", 0, 1, [](const std::vector<Value>& args) -> Value {
        int btn = args.empty() ? 1 : (int)args[0].to_int();
        float x, y;
        SDL_MouseButtonFlags state = SDL_GetMouseState(&x, &y);
        return Value::make_bool((state & SDL_BUTTON_MASK(btn)) != 0);
    });

    vm.register_native("GFX.DELAY", 1, 1, [](const std::vector<Value>& args) -> Value {
        SDL_Delay((Uint32)args[0].to_int());
        return Value::make_none();
    });

    vm.register_native("GFX.TICKS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return Value::make_i64((int64_t)SDL_GetTicks());
    });

    vm.register_native("GFX.CLOSE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        cleanup_graphics();
        return Value::make_none();
    });

    // ── Image loading (SDL3_image) ──────────────────────────────

    vm.register_native("GFX.LOADIMAGE", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.LOADIMAGE");
        std::string path = resolve_asset_path(args[0].as_string()->data);

        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("IMG_Load failed: ") + SDL_GetError());

        SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
        SDL_DestroySurface(surface);
        if (!tex)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("SDL_CreateTextureFromSurface failed: ") + SDL_GetError());

        int id = g_next_image_id++;
        g_images[id] = tex;
        return Value::make_i64(id);
    });

    vm.register_native("GFX.DRAWIMAGE", 2, 6, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.DRAWIMAGE");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.DRAWIMAGE: invalid image id");

        float x = (float)args[1].to_double();
        float y = (args.size() >= 3) ? (float)args[2].to_double() : 0.0f;

        float tw, th;
        SDL_GetTextureSize(it->second, &tw, &th);

        float w = (args.size() >= 4) ? (float)args[3].to_double() : tw;
        float h = (args.size() >= 5) ? (float)args[4].to_double() : th;

        SDL_FRect dst = {x, y, w, h};
        SDL_RenderTexture(g_renderer, it->second, nullptr, &dst);
        return Value::make_none();
    });

    vm.register_native("GFX.FREEIMAGE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it != g_images.end()) {
            SDL_DestroyTexture(it->second);
            g_images.erase(it);
        }
        return Value::make_none();
    });

    // ── Sprite System (Phase 1) ────────────────────────────────

    // SPRITE.LOAD file$ — single image sprite
    // SPRITE.LOAD file$, frame_w, frame_h — spritesheet with frame size
    vm.register_native("SPRITE.LOAD", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_screen("SPRITE.LOAD");
        std::string path = resolve_asset_path(args[0].as_string()->data);
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("SPRITE.LOAD: ") + SDL_GetError());
        float tw = (float)surface->w, th = (float)surface->h;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
        SDL_DestroySurface(surface);
        if (!tex)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("SPRITE.LOAD: ") + SDL_GetError());
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        int img_id = g_next_image_id++;
        g_images[img_id] = tex;

        int fw = (args.size() >= 3) ? (int)args[1].to_int() : 0;
        int fh = (args.size() >= 3) ? (int)args[2].to_int() : 0;

        Sprite sp{};
        sp.texture_id = img_id;
        sp.x = 0; sp.y = 0;
        sp.vx = 0; sp.vy = 0;
        sp.scale_x = 1.0f; sp.scale_y = 1.0f;
        sp.origin_x = 0; sp.origin_y = 0;
        sp.angle = 0; sp.alpha = 255;
        sp.visible = true;
        sp.flip_h = false; sp.flip_v = false;
        sp.tex_w = tw; sp.tex_h = th;
        sp.frame_w = fw; sp.frame_h = fh;
        sp.cols = (fw > 0) ? (int)(tw / fw) : 1;
        sp.w = (fw > 0) ? (float)fw : tw;
        sp.h = (fh > 0) ? (float)fh : th;
        sp.current_frame = 0;
        sp.zorder = 0;
        sp.current_anim = -1;
        sp.anim_timer = 0;
        sp.playing = false;
        sp.gravity = 0;
        sp.on_ground = false;
        int sid = g_next_sprite_id++;
        g_sprites[sid] = sp;
        return Value::make_i64(sid);
    });

    vm.register_native("SPRITE.POS", 3, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.POS", (int)args[0].to_int());
        sp.x = (float)args[1].to_double();
        sp.y = (float)args[2].to_double();
        return Value::make_none();
    });

    vm.register_native("SPRITE.MOVE", 3, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.MOVE", (int)args[0].to_int());
        sp.x += (float)args[1].to_double();
        sp.y += (float)args[2].to_double();
        return Value::make_none();
    });

    vm.register_native("SPRITE.DRAW", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_screen("SPRITE.DRAW");
        Sprite& sp = get_sprite("SPRITE.DRAW", (int)args[0].to_int());
        if (sp.visible) draw_one_sprite(sp);
        return Value::make_none();
    });

    vm.register_native("SPRITE.DRAW_ALL", 0, 2, [](const std::vector<Value>& args) -> Value {
        ensure_screen("SPRITE.DRAW_ALL");
        // Use camera offset (auto from g_cam, or manual override)
        float cam_x = g_cam.x + g_cam.shake_ox;
        float cam_y = g_cam.y + g_cam.shake_oy;
        if (args.size() >= 1) cam_x = (float)args[0].to_double();
        if (args.size() >= 2) cam_y = (float)args[1].to_double();

        // Sort by z-order for drawing
        std::vector<Sprite*> sorted;
        for (auto& [id, sp] : g_sprites) {
            if (sp.visible) sorted.push_back(&sp);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const Sprite* a, const Sprite* b) { return a->zorder < b->zorder; });

        for (auto* sp : sorted) {
            Sprite tmp = *sp;
            tmp.x -= cam_x;
            tmp.y -= cam_y;
            draw_one_sprite(tmp);
        }
        return Value::make_none();
    });

    vm.register_native("SPRITE.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_sprites.erase(id);
        return Value::make_none();
    });

    vm.register_native("SPRITE.GET_X", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(get_sprite("SPRITE.GET_X", (int)args[0].to_int()).x);
    });
    vm.register_native("SPRITE.GET_Y", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(get_sprite("SPRITE.GET_Y", (int)args[0].to_int()).y);
    });

    vm.register_native("SPRITE.SCALE", 2, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.SCALE", (int)args[0].to_int());
        sp.scale_x = (float)args[1].to_double();
        sp.scale_y = (args.size() >= 3) ? (float)args[2].to_double() : sp.scale_x;
        return Value::make_none();
    });

    vm.register_native("SPRITE.FLIP", 2, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.FLIP", (int)args[0].to_int());
        sp.flip_h = args[1].to_bool();
        sp.flip_v = (args.size() >= 3) ? args[2].to_bool() : false;
        return Value::make_none();
    });

    vm.register_native("SPRITE.ALPHA", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.ALPHA", (int)args[0].to_int());
        int a = (int)args[1].to_int();
        sp.alpha = (a < 0) ? 0 : (a > 255) ? 255 : a;
        return Value::make_none();
    });

    vm.register_native("SPRITE.VISIBLE", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.VISIBLE", (int)args[0].to_int());
        sp.visible = args[1].to_bool();
        return Value::make_none();
    });

    vm.register_native("SPRITE.ROTATE", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.ROTATE", (int)args[0].to_int());
        sp.angle = (float)args[1].to_double();
        return Value::make_none();
    });

    vm.register_native("SPRITE.SET_ORIGIN", 3, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.SET_ORIGIN", (int)args[0].to_int());
        sp.origin_x = (float)args[1].to_double();
        sp.origin_y = (float)args[2].to_double();
        return Value::make_none();
    });

    vm.register_native("SPRITE.COLLISION", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& a = get_sprite("SPRITE.COLLISION", (int)args[0].to_int());
        Sprite& b = get_sprite("SPRITE.COLLISION", (int)args[1].to_int());
        if (!a.visible || !b.visible) return Value::make_bool(false);
        float aw = a.w * a.scale_x, ah = a.h * a.scale_y;
        float bw = b.w * b.scale_x, bh = b.h * b.scale_y;
        bool hit = (a.x < b.x + bw) && (a.x + aw > b.x) &&
                   (a.y < b.y + bh) && (a.y + ah > b.y);
        return Value::make_bool(hit);
    });

    vm.register_native("SPRITE.WIDTH", 1, 1, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.WIDTH", (int)args[0].to_int());
        return Value::make_f64(sp.w * sp.scale_x);
    });
    vm.register_native("SPRITE.HEIGHT", 1, 1, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.HEIGHT", (int)args[0].to_int());
        return Value::make_f64(sp.h * sp.scale_y);
    });

    // ── Sprite Animation (Phase 2) ─────────────────────────────

    // SPRITE.ANIM id, name$, frames[], fps, [loop]
    vm.register_native("SPRITE.ANIM", 4, 5, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.ANIM", (int)args[0].to_int());
        std::string name = args[1].as_string()->data;
        auto* farr = args[2].as_array();
        float fps = (float)args[3].to_double();
        bool loop = (args.size() >= 5) ? args[4].to_bool() : true;

        SpriteAnim anim;
        anim.name = name;
        anim.fps = fps;
        anim.loop = loop;
        for (auto& f : farr->elements)
            anim.frames.push_back((int)f.to_int());

        // Replace existing anim with same name, or add new
        for (auto& a : sp.anims) {
            if (a.name == name) { a = anim; return Value::make_none(); }
        }
        sp.anims.push_back(std::move(anim));
        return Value::make_none();
    });

    // SPRITE.PLAY id, name$
    vm.register_native("SPRITE.PLAY", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.PLAY", (int)args[0].to_int());
        std::string name = args[1].as_string()->data;
        // Find animation by name
        for (int i = 0; i < (int)sp.anims.size(); i++) {
            if (sp.anims[i].name == name) {
                if (sp.current_anim != i) {
                    sp.current_anim = i;
                    sp.anim_timer = 0;
                    sp.current_frame = sp.anims[i].frames[0];
                }
                sp.playing = true;
                return Value::make_none();
            }
        }
        throw jdError(ErrCode::RUNTIME_ERROR,
            "SPRITE.PLAY: animation '" + name + "' not found");
    });

    // SPRITE.STOP id
    vm.register_native("SPRITE.STOP", 1, 1, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.STOP", (int)args[0].to_int());
        sp.playing = false;
        return Value::make_none();
    });

    // SPRITE.FRAME id, frame_index — manually set frame
    vm.register_native("SPRITE.FRAME", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.FRAME", (int)args[0].to_int());
        sp.current_frame = (int)args[1].to_int();
        sp.playing = false;
        sp.current_anim = -1;
        return Value::make_none();
    });

    // SPRITE.UPDATE — advance all animations by elapsed time
    vm.register_native("SPRITE.UPDATE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Uint64 now = SDL_GetTicks();
        float dt;
        if (g_last_update_tick == 0) {
            dt = 1.0f / 60.0f; // assume 60fps on first call
        } else {
            dt = (float)(now - g_last_update_tick) / 1000.0f;
        }
        g_last_update_tick = now;
        if (dt > 0.1f) dt = 0.1f; // clamp to avoid jumps

        for (auto& [id, sp] : g_sprites) {
            // Gravity
            if (sp.gravity != 0.0f) {
                sp.vy += sp.gravity * dt;
            }
            // Velocity-based movement
            if (sp.vx != 0.0f || sp.vy != 0.0f) {
                sp.x += sp.vx * dt;
                sp.y += sp.vy * dt;
            }
            // Animation
            if (!sp.playing || sp.current_anim < 0) continue;
            auto& anim = sp.anims[sp.current_anim];
            if (anim.frames.empty() || anim.fps <= 0) continue;

            sp.anim_timer += dt;
            float frame_dur = 1.0f / anim.fps;
            while (sp.anim_timer >= frame_dur) {
                sp.anim_timer -= frame_dur;
                // Find current position in anim.frames
                int pos = 0;
                for (int i = 0; i < (int)anim.frames.size(); i++) {
                    if (anim.frames[i] == sp.current_frame) { pos = i; break; }
                }
                pos++;
                if (pos >= (int)anim.frames.size()) {
                    if (anim.loop) pos = 0;
                    else { pos = (int)anim.frames.size() - 1; sp.playing = false; }
                }
                sp.current_frame = anim.frames[pos];
            }
        }

        // Camera follow + shake
        if (g_cam.follow_id >= 0) {
            auto fit = g_sprites.find(g_cam.follow_id);
            if (fit != g_sprites.end()) {
                float target_x = fit->second.x + fit->second.w * fit->second.scale_x / 2 - g_screen_w / 2;
                float target_y = fit->second.y + fit->second.h * fit->second.scale_y / 2 - g_screen_h / 2;
                float s = g_cam.smooth;
                if (s <= 0) { g_cam.x = target_x; g_cam.y = target_y; }
                else {
                    float f = 1.0f - std::pow(s, dt * 60.0f);
                    g_cam.x += (target_x - g_cam.x) * f;
                    g_cam.y += (target_y - g_cam.y) * f;
                }
            }
        }
        if (g_cam.has_bounds) {
            if (g_cam.x < g_cam.bounds_x) g_cam.x = g_cam.bounds_x;
            if (g_cam.y < g_cam.bounds_y) g_cam.y = g_cam.bounds_y;
            float max_x = g_cam.bounds_x + g_cam.bounds_w - g_screen_w;
            float max_y = g_cam.bounds_y + g_cam.bounds_h - g_screen_h;
            if (max_x > g_cam.bounds_x && g_cam.x > max_x) g_cam.x = max_x;
            if (max_y > g_cam.bounds_y && g_cam.y > max_y) g_cam.y = max_y;
        }
        // Shake decay
        if (g_cam.shake_timer > 0) {
            g_cam.shake_timer -= dt;
            float s = g_cam.shake_intensity * (g_cam.shake_timer > 0 ? 1.0f : 0.0f);
            g_cam.shake_ox = ((float)(rand() % 200 - 100) / 100.0f) * s;
            g_cam.shake_oy = ((float)(rand() % 200 - 100) / 100.0f) * s;
            if (g_cam.shake_timer <= 0) { g_cam.shake_ox = 0; g_cam.shake_oy = 0; }
        }

        // Particle update
        for (auto it = g_particles.begin(); it != g_particles.end(); ) {
            it->life -= dt;
            if (it->life <= 0) { it = g_particles.erase(it); continue; }
            it->vy += it->gravity * dt;
            it->x += it->vx * dt;
            it->y += it->vy * dt;
            ++it;
        }

        return Value::make_none();
    });

    // SPRITE.PLAYING(id) -> bool — is the animation playing?
    vm.register_native("SPRITE.PLAYING", 1, 1, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.PLAYING", (int)args[0].to_int());
        return Value::make_bool(sp.playing);
    });

    // ── Velocity System ───────────────────────────────────────

    // SPRITE.VELOCITY id, vx, vy — set velocity (pixels per second)
    vm.register_native("SPRITE.VELOCITY", 3, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.VELOCITY", (int)args[0].to_int());
        sp.vx = (float)args[1].to_double();
        sp.vy = (float)args[2].to_double();
        return Value::make_none();
    });

    // SPRITE.GET_VX(id) / SPRITE.GET_VY(id)
    vm.register_native("SPRITE.GET_VX", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(get_sprite("SPRITE.GET_VX", (int)args[0].to_int()).vx);
    });
    vm.register_native("SPRITE.GET_VY", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(get_sprite("SPRITE.GET_VY", (int)args[0].to_int()).vy);
    });

    // ── Collision Groups ─────────────────────────────────────

    // SPRITE.GROUP id, group_name$ — assign sprite to a group
    vm.register_native("SPRITE.GROUP", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.GROUP", (int)args[0].to_int());
        sp.group = args[1].as_string()->data;
        return Value::make_none();
    });

    // SPRITE.COLLISIONS(group1$, group2$) → [[id_a, id_b], ...]
    vm.register_native("SPRITE.COLLISIONS", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string g1 = args[0].as_string()->data;
        std::string g2 = args[1].as_string()->data;
        Value result = Value::make_array();
        auto* out = result.as_array();

        // Collect sprites per group
        std::vector<std::pair<int, Sprite*>> group1, group2;
        for (auto& [id, sp] : g_sprites) {
            if (!sp.visible) continue;
            if (sp.group == g1) group1.push_back({id, &sp});
            if (sp.group == g2) group2.push_back({id, &sp});
        }

        // AABB test all pairs
        for (auto& [id_a, a] : group1) {
            float aw = a->w * a->scale_x, ah = a->h * a->scale_y;
            for (auto& [id_b, b] : group2) {
                if (id_a == id_b) continue;
                float bw = b->w * b->scale_x, bh = b->h * b->scale_y;
                if (a->x < b->x + bw && a->x + aw > b->x &&
                    a->y < b->y + bh && a->y + ah > b->y) {
                    Value pair = Value::make_array();
                    pair.as_array()->elements.push_back(Value::make_i64(id_a));
                    pair.as_array()->elements.push_back(Value::make_i64(id_b));
                    out->elements.push_back(std::move(pair));
                }
            }
        }
        return result;
    });

    // SPRITE.COLLISION_FIRST(id, group$) → hit_id or -1
    vm.register_native("SPRITE.COLLISION_FIRST", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& a = get_sprite("SPRITE.COLLISION_FIRST", (int)args[0].to_int());
        std::string grp = args[1].as_string()->data;
        if (!a.visible) return Value::make_i64(-1);
        float aw = a.w * a.scale_x, ah = a.h * a.scale_y;

        for (auto& [id, b] : g_sprites) {
            if (id == (int)args[0].to_int() || !b.visible || b.group != grp) continue;
            float bw = b.w * b.scale_x, bh = b.h * b.scale_y;
            if (a.x < b.x + bw && a.x + aw > b.x &&
                a.y < b.y + bh && a.y + ah > b.y) {
                return Value::make_i64(id);
            }
        }
        return Value::make_i64(-1);
    });

    // ── Tilemap System ─────────────────────────────────────────

    // TILEMAP.CREATE name$, tileset_sprite_id, data[][], tile_w, tile_h
    vm.register_native("TILEMAP.CREATE", 5, 5, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        int ts_id = (int)args[1].to_int();
        auto* data_arr = args[2].as_array();
        int tw = (int)args[3].to_int();
        int th = (int)args[4].to_int();

        // Verify tileset exists
        auto ts_it = g_images.find(ts_id);
        if (ts_it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.CREATE: invalid tileset image id");

        float tex_w, tex_h;
        SDL_GetTextureSize(ts_it->second, &tex_w, &tex_h);

        Tilemap tm;
        tm.tileset_id = ts_id;
        tm.tile_w = tw;
        tm.tile_h = th;
        tm.tileset_cols = (int)(tex_w / tw);
        tm.rows = (int)data_arr->elements.size();
        tm.cols = 0;

        for (int r = 0; r < tm.rows; r++) {
            auto* row = data_arr->elements[r].as_array();
            std::vector<int> row_data;
            for (auto& cell : row->elements)
                row_data.push_back((int)cell.to_int());
            if ((int)row_data.size() > tm.cols) tm.cols = (int)row_data.size();
            tm.data.push_back(std::move(row_data));
        }
        g_tilemaps[name] = std::move(tm);
        return Value::make_none();
    });

    // TILEMAP.DRAW name$, [cam_x], [cam_y]
    vm.register_native("TILEMAP.DRAW", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TILEMAP.DRAW");
        std::string name = args[0].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.DRAW: unknown tilemap '" + name + "'");

        // Use global camera if no explicit offset given
        float cam_x = (args.size() >= 2) ? (float)args[1].to_double() : g_cam.x + g_cam.shake_ox;
        float cam_y = (args.size() >= 3) ? (float)args[2].to_double() : g_cam.y + g_cam.shake_oy;
        auto& tm = it->second;

        auto ts_it = g_images.find(tm.tileset_id);
        if (ts_it == g_images.end()) return Value::make_none();
        SDL_Texture* tex = ts_it->second;

        for (int r = 0; r < tm.rows; r++) {
            for (int c = 0; c < (int)tm.data[r].size(); c++) {
                int tid = tm.data[r][c];
                if (tid <= 0) continue; // 0 = empty/transparent

                // Tile ID is 1-based in the tileset (0 = empty)
                int t = tid - 1;
                int src_col = t % tm.tileset_cols;
                int src_row = t / tm.tileset_cols;
                SDL_FRect src = { (float)(src_col * tm.tile_w), (float)(src_row * tm.tile_h),
                                  (float)tm.tile_w, (float)tm.tile_h };
                SDL_FRect dst = { (float)(c * tm.tile_w) - cam_x,
                                  (float)(r * tm.tile_h) - cam_y,
                                  (float)tm.tile_w, (float)tm.tile_h };
                SDL_RenderTexture(g_renderer, tex, &src, &dst);
            }
        }
        return Value::make_none();
    });

    // TILEMAP.SET name$, col, row, tile_id
    vm.register_native("TILEMAP.SET", 4, 4, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.SET: unknown tilemap '" + name + "'");
        int c = (int)args[1].to_int();
        int r = (int)args[2].to_int();
        int tid = (int)args[3].to_int();
        if (r >= 0 && r < it->second.rows && c >= 0 && c < (int)it->second.data[r].size())
            it->second.data[r][c] = tid;
        return Value::make_none();
    });

    // TILEMAP.GET(name$, col, row) → tile_id
    vm.register_native("TILEMAP.GET", 3, 3, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.GET: unknown tilemap '" + name + "'");
        int c = (int)args[1].to_int();
        int r = (int)args[2].to_int();
        if (r >= 0 && r < it->second.rows && c >= 0 && c < (int)it->second.data[r].size())
            return Value::make_i64(it->second.data[r][c]);
        return Value::make_i64(0);
    });

    // TILEMAP.SIZE(name$) → [cols, rows]
    vm.register_native("TILEMAP.SIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.SIZE: unknown tilemap '" + name + "'");
        Value r = Value::make_array();
        r.as_array()->elements.push_back(Value::make_i64(it->second.cols));
        r.as_array()->elements.push_back(Value::make_i64(it->second.rows));
        return r;
    });

    // TILEMAP.COLLIDES(sprite_id, name$) → bool
    // Checks if any corner of the sprite overlaps a non-zero tile
    vm.register_native("TILEMAP.COLLIDES", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("TILEMAP.COLLIDES", (int)args[0].to_int());
        std::string name = args[1].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.COLLIDES: unknown tilemap '" + name + "'");
        auto& tm = it->second;
        float sw = sp.w * sp.scale_x, sh = sp.h * sp.scale_y;

        // Check all 4 corners of the sprite against the tile grid
        float corners[][2] = {
            {sp.x, sp.y}, {sp.x + sw - 1, sp.y},
            {sp.x, sp.y + sh - 1}, {sp.x + sw - 1, sp.y + sh - 1}
        };
        for (auto& pt : corners) {
            int tc = (int)(pt[0] / tm.tile_w);
            int tr = (int)(pt[1] / tm.tile_h);
            if (tc < 0 || tr < 0 || tr >= tm.rows) continue;
            if (tc >= (int)tm.data[tr].size()) continue;
            if (tm.data[tr][tc] > 0) return Value::make_bool(true);
        }
        return Value::make_bool(false);
    });

    // TILEMAP.TILE_AT(name$, pixel_x, pixel_y) → tile_id
    vm.register_native("TILEMAP.TILE_AT", 3, 3, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto it = g_tilemaps.find(name);
        if (it == g_tilemaps.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "TILEMAP.TILE_AT: unknown tilemap '" + name + "'");
        auto& tm = it->second;
        int tc = (int)(args[1].to_double() / tm.tile_w);
        int tr = (int)(args[2].to_double() / tm.tile_h);
        if (tr >= 0 && tr < tm.rows && tc >= 0 && tc < (int)tm.data[tr].size())
            return Value::make_i64(tm.data[tr][tc]);
        return Value::make_i64(0);
    });

    // ── Camera System ──────────────────────────────────────────

    vm.register_native("CAM.FOLLOW", 1, 2, [](const std::vector<Value>& args) -> Value {
        g_cam.follow_id = (int)args[0].to_int();
        if (args.size() >= 2) g_cam.smooth = (float)args[1].to_double();
        return Value::make_none();
    });
    vm.register_native("CAM.SET", 2, 2, [](const std::vector<Value>& args) -> Value {
        g_cam.x = (float)args[0].to_double();
        g_cam.y = (float)args[1].to_double();
        g_cam.follow_id = -1;
        return Value::make_none();
    });
    vm.register_native("CAM.BOUNDS", 4, 4, [](const std::vector<Value>& args) -> Value {
        g_cam.bounds_x = (float)args[0].to_double();
        g_cam.bounds_y = (float)args[1].to_double();
        g_cam.bounds_w = (float)args[2].to_double();
        g_cam.bounds_h = (float)args[3].to_double();
        g_cam.has_bounds = true;
        return Value::make_none();
    });
    vm.register_native("CAM.SHAKE", 2, 2, [](const std::vector<Value>& args) -> Value {
        g_cam.shake_intensity = (float)args[0].to_double();
        g_cam.shake_timer = (float)args[1].to_double();
        return Value::make_none();
    });
    vm.register_native("CAM.X", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(g_cam.x + g_cam.shake_ox);
    });
    vm.register_native("CAM.Y", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_f64(g_cam.y + g_cam.shake_oy);
    });

    // ── Z-Order ─────────────────────────────────────────────────

    vm.register_native("SPRITE.ZORDER", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.ZORDER", (int)args[0].to_int());
        sp.zorder = (int)args[1].to_int();
        return Value::make_none();
    });

    // ── Sprite Physics ──────────────────────────────────────────

    vm.register_native("SPRITE.GRAVITY", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.GRAVITY", (int)args[0].to_int());
        sp.gravity = (float)args[1].to_double();
        return Value::make_none();
    });
    vm.register_native("SPRITE.ON_GROUND", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(get_sprite("SPRITE.ON_GROUND", (int)args[0].to_int()).on_ground);
    });

    // SPRITE.LAND id, ground_y — snap to ground and zero vertical velocity
    vm.register_native("SPRITE.LAND", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.LAND", (int)args[0].to_int());
        float ground_y = (float)args[1].to_double();
        float sh = sp.h * sp.scale_y;
        if (sp.y + sh > ground_y) {
            sp.y = ground_y - sh;
            sp.vy = 0;
            sp.on_ground = true;
        } else {
            sp.on_ground = false;
        }
        return Value::make_none();
    });

    // ── Particle System ─────────────────────────────────────────

    // PARTICLE.EMIT x, y, count, r, g, b, [speed], [life], [gravity], [size]
    vm.register_native("PARTICLE.EMIT", 6, 10, [](const std::vector<Value>& args) -> Value {
        float px = (float)args[0].to_double();
        float py = (float)args[1].to_double();
        int count = (int)args[2].to_int();
        Uint8 r = (Uint8)args[3].to_int();
        Uint8 g = (Uint8)args[4].to_int();
        Uint8 b = (Uint8)args[5].to_int();
        float speed = (args.size() >= 7) ? (float)args[6].to_double() : 50.0f;
        float life = (args.size() >= 8) ? (float)args[7].to_double() : 1.0f;
        float grav = (args.size() >= 9) ? (float)args[8].to_double() : 0.0f;
        float size = (args.size() >= 10) ? (float)args[9].to_double() : 2.0f;

        for (int i = 0; i < count; i++) {
            float angle = ((float)(rand() % 3600) / 10.0f) * (3.14159f / 180.0f);
            float spd = speed * (0.5f + (float)(rand() % 100) / 100.0f);
            Particle p;
            p.x = px; p.y = py;
            p.vx = cosf(angle) * spd;
            p.vy = sinf(angle) * spd;
            p.life = life * (0.5f + (float)(rand() % 100) / 200.0f);
            p.max_life = p.life;
            p.r = r; p.g = g; p.b = b;
            p.size = size;
            p.gravity = grav;
            g_particles.push_back(p);
        }
        return Value::make_none();
    });

    // PARTICLE.DRAW [cam_x, cam_y] — draw all particles
    vm.register_native("PARTICLE.DRAW", 0, 2, [](const std::vector<Value>& args) -> Value {
        ensure_screen("PARTICLE.DRAW");
        float cx = g_cam.x + g_cam.shake_ox;
        float cy = g_cam.y + g_cam.shake_oy;
        if (args.size() >= 1) cx = (float)args[0].to_double();
        if (args.size() >= 2) cy = (float)args[1].to_double();

        for (auto& p : g_particles) {
            float alpha_f = p.life / p.max_life;
            Uint8 a = (Uint8)(alpha_f * 255);
            SDL_SetRenderDrawColor(g_renderer, p.r, p.g, p.b, a);
            SDL_FRect dst = { p.x - cx - p.size/2, p.y - cy - p.size/2, p.size, p.size };
            SDL_RenderFillRect(g_renderer, &dst);
        }
        apply_draw_color();
        return Value::make_none();
    });

    // PARTICLE.CLEAR — remove all particles
    vm.register_native("PARTICLE.CLEAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; g_particles.clear(); return Value::make_none();
    });

    // PARTICLE.COUNT() -> int
    vm.register_native("PARTICLE.COUNT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args; return Value::make_i64(g_particles.size());
    });

    // ── TILEMAP.DRAW with auto-camera ───────────────────────────
    // Patch: TILEMAP.DRAW now uses camera automatically when no args given

    // ── GFX.CAPTURE / GFX.DRAW_CAPTURE ────────────────────────
    // Captures the current renderer content as a reusable background texture.
    // Use before dialogs/menus to freeze the game scene.

    vm.register_native("GFX.CAPTURE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_screen("GFX.CAPTURE");
        // Read current renderer into a surface, then create texture
        SDL_Surface* surf = SDL_RenderReadPixels(g_renderer, nullptr);
        if (!surf) return Value::make_none();
        SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surf);
        SDL_DestroySurface(surf);
        if (!tex) return Value::make_none();
        // Store as a special image
        int id = g_next_image_id++;
        g_images[id] = tex;
        return Value::make_i64(id);
    });

    vm.register_native("GFX.DRAW_CAPTURE", 1, 1, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.DRAW_CAPTURE");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end()) return Value::make_none();
        SDL_RenderTexture(g_renderer, it->second, nullptr, nullptr);
        return Value::make_none();
    });

    // ── GFX.FADE direction, duration, [r, g, b] ──────────────
    // direction: 0 = fade OUT (to black), 1 = fade IN (from black)
    // Blocks until fade is complete, calling SCREENFLIP each frame.

    vm.register_native("GFX.FADE", 2, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.FADE");
        int direction = (int)args[0].to_int(); // 0=out, 1=in
        float duration = (float)args[1].to_double();
        Uint8 fr = (args.size() >= 3) ? (Uint8)args[2].to_int() : 0;
        Uint8 fg = (args.size() >= 4) ? (Uint8)args[3].to_int() : 0;
        Uint8 fb = (args.size() >= 5) ? (Uint8)args[4].to_int() : 0;

        if (duration <= 0) duration = 0.01f;

        // Capture current screen as background
        SDL_Surface* cap_surf = SDL_RenderReadPixels(g_renderer, nullptr);
        SDL_Texture* cap_tex = cap_surf ? SDL_CreateTextureFromSurface(g_renderer, cap_surf) : nullptr;
        if (cap_surf) SDL_DestroySurface(cap_surf);

        Uint64 start = SDL_GetTicks();
        float dur_ms = duration * 1000.0f;

        while (true) {
            float elapsed = (float)(SDL_GetTicks() - start);
            float t = elapsed / dur_ms;
            if (t > 1.0f) t = 1.0f;

            Uint8 alpha = (direction == 0)
                ? (Uint8)(t * 255.0f)
                : (Uint8)((1.0f - t) * 255.0f);

            // Redraw captured background each frame
            if (cap_tex) SDL_RenderTexture(g_renderer, cap_tex, nullptr, nullptr);

            // Overlay
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_renderer, fr, fg, fb, alpha);
            SDL_FRect full = {0, 0, (float)g_screen_w, (float)g_screen_h};
            SDL_RenderFillRect(g_renderer, &full);
            SDL_RenderPresent(g_renderer);

            if (t >= 1.0f) break;
            SDL_Delay(16);
        }
        if (cap_tex) SDL_DestroyTexture(cap_tex);
        apply_draw_color();
        return Value::make_none();
    });

    // ── GFX.SAVE_IMAGE id, filename$ ──────────────────────────
    // Saves a loaded (and possibly modified) image to PNG

    vm.register_native("GFX.SAVE_IMAGE", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.SAVE_IMAGE");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.SAVE_IMAGE: invalid image id");
        std::string path = args[1].as_string()->data;

        SDL_Texture* tex = it->second;
        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);
        int w = (int)tw, h = (int)th;

        // Render texture to a temp target with transparent background
        SDL_Texture* target = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET, w, h);
        if (!target)
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.SAVE_IMAGE: could not create target");

        SDL_Texture* old_target = SDL_GetRenderTarget(g_renderer);
        SDL_SetRenderTarget(g_renderer, target);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 0);
        SDL_RenderClear(g_renderer);
        SDL_RenderTexture(g_renderer, tex, nullptr, nullptr);

        SDL_Surface* surf = SDL_RenderReadPixels(g_renderer, nullptr);
        SDL_SetRenderTarget(g_renderer, old_target);
        SDL_DestroyTexture(target);

        if (!surf)
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.SAVE_IMAGE: read pixels failed");

        bool ok = IMG_SavePNG(surf, path.c_str());
        SDL_DestroySurface(surf);
        if (!ok)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GFX.SAVE_IMAGE: ") + SDL_GetError());
        return Value::make_none();
    });

    // ── GFX.SAVE_SCREENSHOT filename$ ──────────────────────────

    // GFX.SAVE_SCREENSHOT file$, [x, y, w, h] — save full screen or a region
    vm.register_native("GFX.SAVE_SCREENSHOT", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.SAVE_SCREENSHOT");
        std::string path = args[0].as_string()->data;

        SDL_Rect region_rect;
        SDL_Rect* rect_ptr = nullptr;
        if (args.size() >= 5) {
            region_rect.x = (int)args[1].to_int();
            region_rect.y = (int)args[2].to_int();
            region_rect.w = (int)args[3].to_int();
            region_rect.h = (int)args[4].to_int();
            rect_ptr = &region_rect;
        }

        SDL_Surface* surface = SDL_RenderReadPixels(g_renderer, rect_ptr);
        if (!surface)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GFX.SAVE_SCREENSHOT: ") + SDL_GetError());

        bool ok = IMG_SavePNG(surface, path.c_str());
        SDL_DestroySurface(surface);
        if (!ok)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GFX.SAVE_SCREENSHOT: ") + SDL_GetError());
        return Value::make_none();
    });

    // ── GFX.DRAWIMAGE_REGION id, sx, sy, sw, sh, dx, dy, [dw, dh] ──

    vm.register_native("GFX.DRAWIMAGE_REGION", 7, 9, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.DRAWIMAGE_REGION");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.DRAWIMAGE_REGION: invalid image id");

        SDL_FRect src = {
            (float)args[1].to_double(), (float)args[2].to_double(),
            (float)args[3].to_double(), (float)args[4].to_double()
        };
        float dw = (args.size() >= 8) ? (float)args[7].to_double() : src.w;
        float dh = (args.size() >= 9) ? (float)args[8].to_double() : src.h;
        SDL_FRect dst = {
            (float)args[5].to_double(), (float)args[6].to_double(), dw, dh
        };
        SDL_RenderTexture(g_renderer, it->second, &src, &dst);
        return Value::make_none();
    });

    // ── GFX.DRAWIMAGE_EX id, x, y, [w, h], [angle], [flip_h] ───

    vm.register_native("GFX.DRAWIMAGE_EX", 3, 7, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.DRAWIMAGE_EX");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.DRAWIMAGE_EX: invalid image id");

        float x = (float)args[1].to_double();
        float y = (float)args[2].to_double();
        float tw, th;
        SDL_GetTextureSize(it->second, &tw, &th);
        float w = (args.size() >= 4) ? (float)args[3].to_double() : tw;
        float h = (args.size() >= 5) ? (float)args[4].to_double() : th;
        double angle = (args.size() >= 6) ? args[5].to_double() : 0.0;
        bool flip_h = (args.size() >= 7) ? args[6].to_bool() : false;

        SDL_FRect dst = { x, y, w, h };
        SDL_FlipMode flip = flip_h ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderTextureRotated(g_renderer, it->second, nullptr, &dst, angle, nullptr, flip);
        return Value::make_none();
    });

    // ── GFX.COLOR_TO_ALPHA id, [tolerance] ─────────────────────
    // Flood-fill from corners to remove background.
    // Auto-detects the bg color from the top-left corner pixel.
    // Only removes CONNECTED background pixels (not similar colors inside the character).

    vm.register_native("GFX.COLOR_TO_ALPHA", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.COLOR_TO_ALPHA");
        int id = (int)args[0].to_int();
        auto it = g_images.find(id);
        if (it == g_images.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.COLOR_TO_ALPHA: invalid image id");

        int tolerance = 40;
        int manual_r = -1, manual_g = -1, manual_b = -1;
        if (args.size() == 2) {
            tolerance = (int)args[1].to_int();
        } else if (args.size() >= 4) {
            manual_r = (int)args[1].to_int();
            manual_g = (int)args[2].to_int();
            manual_b = (int)args[3].to_int();
            if (args.size() >= 5) tolerance = (int)args[4].to_int();
        }

        SDL_Texture* tex = it->second;
        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);
        int w = (int)tw, h = (int)th;

        // Read texture pixels via render target
        SDL_Texture* target = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET, w, h);
        if (!target) return Value::make_none();
        SDL_Texture* old_target = SDL_GetRenderTarget(g_renderer);
        SDL_SetRenderTarget(g_renderer, target);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 0);
        SDL_RenderClear(g_renderer);
        SDL_RenderTexture(g_renderer, tex, nullptr, nullptr);
        SDL_Surface* surf = SDL_RenderReadPixels(g_renderer, nullptr);
        SDL_SetRenderTarget(g_renderer, old_target);
        SDL_DestroyTexture(target);
        if (!surf) return Value::make_none();

        int pitch = surf->pitch;
        Uint8* px = (Uint8*)surf->pixels;

        // Detect bg color from top-left corner (or use manual)
        Uint8 bg_r, bg_g, bg_b;
        if (manual_r >= 0) {
            bg_r = (Uint8)manual_r; bg_g = (Uint8)manual_g; bg_b = (Uint8)manual_b;
        } else {
            bg_r = px[0]; bg_g = px[1]; bg_b = px[2];
        }

        // Flood-fill from all 4 corners + edges
        std::vector<bool> visited(w * h, false);
        std::vector<std::pair<int,int>> stack;

        // Seed from all 4 corners
        stack.push_back({0, 0});
        stack.push_back({w-1, 0});
        stack.push_back({0, h-1});
        stack.push_back({w-1, h-1});
        // Also seed from edges every 4 pixels (catches bg behind gaps)
        for (int x = 0; x < w; x += 4) { stack.push_back({x, 0}); stack.push_back({x, h-1}); }
        for (int y = 0; y < h; y += 4) { stack.push_back({0, y}); stack.push_back({w-1, y}); }

        while (!stack.empty()) {
            auto [x, y] = stack.back(); stack.pop_back();
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            int idx = y * w + x;
            if (visited[idx]) continue;
            int off = y * pitch + x * 4;
            int dr = abs((int)px[off+0] - (int)bg_r);
            int dg = abs((int)px[off+1] - (int)bg_g);
            int db = abs((int)px[off+2] - (int)bg_b);
            if (dr > tolerance || dg > tolerance || db > tolerance) continue;
            visited[idx] = true;
            px[off + 3] = 0; // make transparent
            stack.push_back({x+1, y}); stack.push_back({x-1, y});
            stack.push_back({x, y+1}); stack.push_back({x, y-1});
        }

        SDL_Texture* new_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
        SDL_DestroySurface(surf);
        if (new_tex) {
            SDL_SetTextureBlendMode(new_tex, SDL_BLENDMODE_BLEND);
            SDL_DestroyTexture(tex);
            g_images[id] = new_tex;
        }
        return Value::make_none();
    });

    // ── Text size query ─────────────────────────────────────────

    vm.register_native("GFX.TEXTSIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!g_font)
            throw jdError(ErrCode::RUNTIME_ERROR, "GFX.TEXTSIZE: no font (call SETFONT first)");
        std::string text = args[0].to_string();
        int w = 0, h = 0;
        TTF_GetStringSize(g_font, text.c_str(), 0, &w, &h);
        Value result = Value::make_array();
        result.as_array()->elements.push_back(Value::make_i64(w));
        result.as_array()->elements.push_back(Value::make_i64(h));
        return result;
    });

    // ══════════════════════════════════════════════════════════════
    // ── Audio (SDL3_mixer 3.4+ — track/audio model) ──────────────
    // ══════════════════════════════════════════════════════════════

    vm.register_native("AUDIO.INIT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (g_audio_init) return Value::make_none();

        if (!g_sdl_init) {
            if (!SDL_Init(SDL_INIT_AUDIO)) {
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("SDL_Init(AUDIO) failed: ") + SDL_GetError());
            }
        } else {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
        }

        if (!MIX_Init())
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("MIX_Init failed: ") + SDL_GetError());

        SDL_AudioSpec spec;
        spec.freq = 44100;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;

        g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (!g_mixer)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("MIX_CreateMixerDevice failed: ") + SDL_GetError());

        g_audio_init = true;
        return Value::make_none();
    });

    // Sound effects: predecoded MIX_Audio + one MIX_Track per chunk id.
    // Old API supported concurrent play of the same chunk on different
    // channels — that collapses to a single track per id that restarts
    // on play. Add a track pool here later if real overlap is needed.
    static std::unordered_map<int, MIX_Audio*> s_chunks;
    static std::unordered_map<int, MIX_Track*> s_chunk_tracks;
    static int s_next_chunk_id = 1;

    vm.register_native("AUDIO.LOADWAV", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!g_audio_init)
            throw jdError(ErrCode::RUNTIME_ERROR, "AUDIO.LOADWAV: call AUDIO.INIT first");
        std::string path = resolve_asset_path(args[0].as_string()->data);
        MIX_Audio* audio = MIX_LoadAudio(g_mixer, path.c_str(), true);
        if (!audio)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("MIX_LoadAudio failed: ") + SDL_GetError());
        int id = s_next_chunk_id++;
        s_chunks[id] = audio;
        return Value::make_i64(id);
    });

    vm.register_native("AUDIO.PLAY", 1, 3, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        int loops = (args.size() >= 2) ? (int)args[1].to_int() : 0;
        auto it = s_chunks.find(id);
        if (it == s_chunks.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "AUDIO.PLAY: invalid sound id");

        MIX_Track* track;
        auto tit = s_chunk_tracks.find(id);
        if (tit == s_chunk_tracks.end()) {
            track = MIX_CreateTrack(g_mixer);
            s_chunk_tracks[id] = track;
            MIX_SetTrackAudio(track, it->second);
        } else {
            track = tit->second;
            MIX_StopTrack(track, 0);
        }
        MIX_SetTrackLoops(track, loops);
        MIX_PlayTrack(track, 0);
        return Value::make_i64(id);
    });

    vm.register_native("AUDIO.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto tit = s_chunk_tracks.find(id);
        if (tit != s_chunk_tracks.end()) {
            MIX_DestroyTrack(tit->second);
            s_chunk_tracks.erase(tit);
        }
        auto it = s_chunks.find(id);
        if (it != s_chunks.end()) {
            MIX_DestroyAudio(it->second);
            s_chunks.erase(it);
        }
        return Value::make_none();
    });

    // Music: streamed MIX_Audio + a single dedicated MIX_Track shared
    // across all music ids (only one piece of music plays at a time, same
    // as the old Mix_PlayMusic semantics).
    static std::unordered_map<int, MIX_Audio*> s_musics;
    static int s_next_music_id = 1;
    static MIX_Track* s_music_track = nullptr;

    vm.register_native("AUDIO.LOADMUS", 1, 1, [](const std::vector<Value>& args) -> Value {
        if (!g_audio_init)
            throw jdError(ErrCode::RUNTIME_ERROR, "AUDIO.LOADMUS: call AUDIO.INIT first");
        std::string path = resolve_asset_path(args[0].as_string()->data);
        MIX_Audio* audio = MIX_LoadAudio(g_mixer, path.c_str(), false);
        if (!audio)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("MIX_LoadAudio failed: ") + SDL_GetError());
        int id = s_next_music_id++;
        s_musics[id] = audio;
        return Value::make_i64(id);
    });

    vm.register_native("AUDIO.PLAYMUS", 1, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        int loops = (args.size() >= 2) ? (int)args[1].to_int() : -1;
        auto it = s_musics.find(id);
        if (it == s_musics.end())
            throw jdError(ErrCode::RUNTIME_ERROR, "AUDIO.PLAYMUS: invalid music id");
        if (!s_music_track) s_music_track = MIX_CreateTrack(g_mixer);
        MIX_StopTrack(s_music_track, 0);
        MIX_SetTrackAudio(s_music_track, it->second);
        MIX_SetTrackLoops(s_music_track, loops);
        MIX_PlayTrack(s_music_track, 0);
        return Value::make_none();
    });

    vm.register_native("AUDIO.FREEMUS", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        auto it = s_musics.find(id);
        if (it != s_musics.end()) {
            if (s_music_track && MIX_GetTrackAudio(s_music_track) == it->second)
                MIX_StopTrack(s_music_track, 0);
            MIX_DestroyAudio(it->second);
            s_musics.erase(it);
        }
        return Value::make_none();
    });

    vm.register_native("AUDIO.STOP", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty() || (int)args[0].to_int() < 0) {
            for (auto& kv : s_chunk_tracks) MIX_StopTrack(kv.second, 0);
        } else {
            int id = (int)args[0].to_int();
            auto it = s_chunk_tracks.find(id);
            if (it != s_chunk_tracks.end()) MIX_StopTrack(it->second, 0);
        }
        return Value::make_none();
    });

    vm.register_native("AUDIO.STOPMUS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (s_music_track) MIX_StopTrack(s_music_track, 0);
        return Value::make_none();
    });

    vm.register_native("AUDIO.VOLUME", 1, 2, [](const std::vector<Value>& args) -> Value {
        int vol = (int)args[0].to_int(); // 0-128 (compat)
        float gain = (float)vol / 128.0f;
        if (args.size() >= 2 && (int)args[1].to_int() >= 0) {
            int id = (int)args[1].to_int();
            auto it = s_chunk_tracks.find(id);
            if (it != s_chunk_tracks.end()) MIX_SetTrackGain(it->second, gain);
        } else if (g_mixer) {
            MIX_SetMixerGain(g_mixer, gain);
        }
        return Value::make_i64(vol);
    });

    vm.register_native("AUDIO.VOLUMEMUS", 1, 1, [](const std::vector<Value>& args) -> Value {
        int vol = (int)args[0].to_int();
        if (s_music_track) MIX_SetTrackGain(s_music_track, (float)vol / 128.0f);
        return Value::make_i64(vol);
    });

    vm.register_native("AUDIO.PAUSE", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty() || (int)args[0].to_int() < 0) {
            if (g_mixer) MIX_PauseAllTracks(g_mixer);
        } else {
            int id = (int)args[0].to_int();
            auto it = s_chunk_tracks.find(id);
            if (it != s_chunk_tracks.end()) MIX_PauseTrack(it->second);
        }
        return Value::make_none();
    });

    vm.register_native("AUDIO.RESUME", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty() || (int)args[0].to_int() < 0) {
            if (g_mixer) MIX_ResumeAllTracks(g_mixer);
        } else {
            int id = (int)args[0].to_int();
            auto it = s_chunk_tracks.find(id);
            if (it != s_chunk_tracks.end()) MIX_ResumeTrack(it->second);
        }
        return Value::make_none();
    });

    vm.register_native("AUDIO.PAUSEMUS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (s_music_track) MIX_PauseTrack(s_music_track);
        return Value::make_none();
    });

    vm.register_native("AUDIO.RESUMEMUS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (s_music_track) MIX_ResumeTrack(s_music_track);
        return Value::make_none();
    });

    vm.register_native("AUDIO.CLOSE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        for (auto& kv : s_chunk_tracks) MIX_DestroyTrack(kv.second);
        s_chunk_tracks.clear();
        for (auto& kv : s_chunks) MIX_DestroyAudio(kv.second);
        s_chunks.clear();
        if (s_music_track) { MIX_DestroyTrack(s_music_track); s_music_track = nullptr; }
        for (auto& kv : s_musics) MIX_DestroyAudio(kv.second);
        s_musics.clear();
        if (g_mixer) { MIX_DestroyMixer(g_mixer); g_mixer = nullptr; }
        if (g_audio_init) { MIX_Quit(); g_audio_init = false; }
        return Value::make_none();
    });

    // ══════════════════════════════════════════════════════════════
    // ── Mouse (short names, logical coordinates) ─────────────────
    // ══════════════════════════════════════════════════════════════

    vm.register_native("MOUSEX", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float wx, wy;
        SDL_GetMouseState(&wx, &wy);
        if (g_renderer) {
            float rx, ry;
            SDL_RenderCoordinatesFromWindow(g_renderer, wx, wy, &rx, &ry);
            return Value::make_i64((int64_t)rx);
        }
        return Value::make_i64((int64_t)wx);
    });
    vm.register_native("MOUSEY", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float wx, wy;
        SDL_GetMouseState(&wx, &wy);
        if (g_renderer) {
            float rx, ry;
            SDL_RenderCoordinatesFromWindow(g_renderer, wx, wy, &rx, &ry);
            return Value::make_i64((int64_t)ry);
        }
        return Value::make_i64((int64_t)wy);
    });
    vm.register_native("MOUSEB", 1, 1, [](const std::vector<Value>& args) -> Value {
        int btn = (int)args[0].to_int();
        float x, y;
        SDL_MouseButtonFlags state = SDL_GetMouseState(&x, &y);
        return Value::make_bool((state & SDL_BUTTON_MASK(btn)) != 0);
    });

    // ══════════════════════════════════════════════════════════════
    // ── Turtle Graphics ──────────────────────────────────────────
    // ══════════════════════════════════════════════════════════════

    // Turtle state
    static float turtle_x, turtle_y;
    static float turtle_angle = 0.0f;   // degrees, 0 = up/north
    static bool  turtle_pen = true;
    static Uint8 turtle_r = 255, turtle_g = 255, turtle_b = 255;

    struct TurtleSegment { float x1, y1, x2, y2; Uint8 r, g, b; };
    static std::vector<TurtleSegment> turtle_path;
    static bool turtle_inited = false;

    auto turtle_init = [&]() {
        if (!turtle_inited && g_renderer) {
            turtle_x = g_screen_w / 2.0f;
            turtle_y = g_screen_h / 2.0f;
            turtle_angle = 0;
            turtle_pen = true;
            turtle_r = 255; turtle_g = 255; turtle_b = 255;
            turtle_inited = true;
        }
    };

    vm.register_native("TURTLE.FORWARD", 1, 1, [turtle_init](const std::vector<Value>& args) -> Value {
        ensure_screen("TURTLE.FORWARD");
        turtle_init();
        float dist = (float)args[0].to_double();
        float rad = (turtle_angle - 90.0f) * (float)M_PI / 180.0f;
        float nx = turtle_x + dist * std::cos(rad);
        float ny = turtle_y + dist * std::sin(rad);
        if (turtle_pen) {
            SDL_SetRenderDrawColor(g_renderer, turtle_r, turtle_g, turtle_b, 255);
            SDL_RenderLine(g_renderer, turtle_x, turtle_y, nx, ny);
            turtle_path.push_back({turtle_x, turtle_y, nx, ny, turtle_r, turtle_g, turtle_b});
            apply_draw_color();
        }
        turtle_x = nx; turtle_y = ny;
        return Value::make_none();
    });
    vm.register_native("TURTLE.BACKWARD", 1, 1, [turtle_init](const std::vector<Value>& args) -> Value {
        ensure_screen("TURTLE.BACKWARD");
        turtle_init();
        float dist = -(float)args[0].to_double();
        float rad = (turtle_angle - 90.0f) * (float)M_PI / 180.0f;
        float nx = turtle_x + dist * std::cos(rad);
        float ny = turtle_y + dist * std::sin(rad);
        if (turtle_pen) {
            SDL_SetRenderDrawColor(g_renderer, turtle_r, turtle_g, turtle_b, 255);
            SDL_RenderLine(g_renderer, turtle_x, turtle_y, nx, ny);
            turtle_path.push_back({turtle_x, turtle_y, nx, ny, turtle_r, turtle_g, turtle_b});
            apply_draw_color();
        }
        turtle_x = nx; turtle_y = ny;
        return Value::make_none();
    });
    vm.register_native("TURTLE.LEFT", 1, 1, [turtle_init](const std::vector<Value>& args) -> Value {
        turtle_init();
        turtle_angle -= (float)args[0].to_double();
        return Value::make_none();
    });
    vm.register_native("TURTLE.RIGHT", 1, 1, [turtle_init](const std::vector<Value>& args) -> Value {
        turtle_init();
        turtle_angle += (float)args[0].to_double();
        return Value::make_none();
    });
    vm.register_native("TURTLE.PENUP", 0, 0, [turtle_init](const std::vector<Value>& args) -> Value {
        (void)args; turtle_init(); turtle_pen = false;
        return Value::make_none();
    });
    vm.register_native("TURTLE.PENDOWN", 0, 0, [turtle_init](const std::vector<Value>& args) -> Value {
        (void)args; turtle_init(); turtle_pen = true;
        return Value::make_none();
    });
    vm.register_native("TURTLE.SETPOS", 2, 2, [turtle_init](const std::vector<Value>& args) -> Value {
        turtle_init();
        turtle_x = (float)args[0].to_double();
        turtle_y = (float)args[1].to_double();
        return Value::make_none();
    });
    vm.register_native("TURTLE.SETHEADING", 1, 1, [turtle_init](const std::vector<Value>& args) -> Value {
        turtle_init();
        turtle_angle = (float)args[0].to_double();
        return Value::make_none();
    });
    vm.register_native("TURTLE.HOME", 0, 0, [turtle_init](const std::vector<Value>& args) -> Value {
        (void)args; turtle_init();
        turtle_x = g_screen_w / 2.0f;
        turtle_y = g_screen_h / 2.0f;
        turtle_angle = 0;
        return Value::make_none();
    });
    vm.register_native("TURTLE.DRAW", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_screen("TURTLE.DRAW");
        for (auto& seg : turtle_path) {
            SDL_SetRenderDrawColor(g_renderer, seg.r, seg.g, seg.b, 255);
            SDL_RenderLine(g_renderer, seg.x1, seg.y1, seg.x2, seg.y2);
        }
        apply_draw_color();
        return Value::make_none();
    });
    vm.register_native("TURTLE.CLEAR", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        turtle_path.clear();
        return Value::make_none();
    });
    vm.register_native("TURTLE.SET_COLOR", 3, 3, [turtle_init](const std::vector<Value>& args) -> Value {
        turtle_init();
        turtle_r = (Uint8)args[0].to_int();
        turtle_g = (Uint8)args[1].to_int();
        turtle_b = (Uint8)args[2].to_int();
        return Value::make_none();
    });

    // ══════════════════════════════════════════════════════════════
    // ── Joystick / Gamepad ───────────────────────────────────────
    // ══════════════════════════════════════════════════════════════

    static std::vector<SDL_Joystick*> s_joysticks;

    auto joy_refresh = []() {
        // SDL_Quit (called by gfx_shutdown / END) tears down ALL
        // subsystems, including SDL_INIT_JOYSTICK. The next SCREEN
        // re-inits VIDEO+EVENTS but NOT JOYSTICK. If we naively
        // reused our cached `s_joysticks` handles after that, we'd
        // be calling SDL_CloseJoystick on freed pointers. Detect
        // the post-Quit state via SDL_WasInit and start fresh.
        if (!SDL_WasInit(SDL_INIT_JOYSTICK)) {
            // Subsystem went away (or never came up): drop any stale
            // handles WITHOUT closing — SDL_Quit already did that —
            // then re-init.
            s_joysticks.clear();
            SDL_InitSubSystem(SDL_INIT_JOYSTICK);
        }
        // Subsystem is alive: close currently-open handles cleanly.
        for (auto* j : s_joysticks) if (j) SDL_CloseJoystick(j);
        s_joysticks.clear();
        // Open all connected joysticks
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        if (ids) {
            for (int i = 0; i < count; i++) {
                SDL_Joystick* j = SDL_OpenJoystick(ids[i]);
                s_joysticks.push_back(j); // may be nullptr
            }
            SDL_free(ids);
        }
    };

    vm.register_native("JOY.COUNT", 0, 0, [joy_refresh](const std::vector<Value>& args) -> Value {
        (void)args;
        joy_refresh();
        return Value::make_i64((int64_t)s_joysticks.size());
    });
    vm.register_native("JOY.NAME$", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        if (id < 0 || id >= (int)s_joysticks.size() || !s_joysticks[id])
            return Value::make_string("");
        const char* name = SDL_GetJoystickName(s_joysticks[id]);
        return Value::make_string(name ? name : "");
    });
    vm.register_native("JOY.BUTTON", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        int btn = (int)args[1].to_int();
        if (id < 0 || id >= (int)s_joysticks.size() || !s_joysticks[id])
            return Value::make_bool(false);
        return Value::make_bool(SDL_GetJoystickButton(s_joysticks[id], btn));
    });
    vm.register_native("JOY.AXIS", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        int axis = (int)args[1].to_int();
        if (id < 0 || id >= (int)s_joysticks.size() || !s_joysticks[id])
            return Value::make_f64(0.0);
        Sint16 raw = SDL_GetJoystickAxis(s_joysticks[id], axis);
        return Value::make_f64(raw / 32767.0);
    });
    vm.register_native("JOY.HAT", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        int hat = (int)args[1].to_int();
        if (id < 0 || id >= (int)s_joysticks.size() || !s_joysticks[id])
            return Value::make_i64(0);
        Uint8 state = SDL_GetJoystickHat(s_joysticks[id], hat);
        // Convert SDL hat mask to our format: 1=up, 2=right, 4=down, 8=left
        int result = 0;
        if (state & SDL_HAT_UP)    result |= 1;
        if (state & SDL_HAT_RIGHT) result |= 2;
        if (state & SDL_HAT_DOWN)  result |= 4;
        if (state & SDL_HAT_LEFT)  result |= 8;
        return Value::make_i64(result);
    });

    // ── Tiled Map System (TMX/TSX) ────────────────────────────────

    // TILED.LOAD(name$, filename$) → bool
    vm.register_native("TILED.LOAD", 2, 2, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TILED.LOAD");
        std::string name = args[0].as_string()->data;
        std::string filename = resolve_asset_path(args[1].as_string()->data);
        bool ok = g_tiled.load_map(name, filename);
        return Value::make_bool(ok);
    });

    // TILED.FREE(name$)
    vm.register_native("TILED.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        g_tiled.free_map(name);
        return Value::make_none();
    });

    // TILED.UPDATE(dt) — update tile animations (dt in seconds)
    vm.register_native("TILED.UPDATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_tiled.update((float)args[0].to_double());
        return Value::make_none();
    });

    // TILED.DRAW name$ [, cam_x, cam_y] — draw all layers in order
    vm.register_native("TILED.DRAW", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TILED.DRAW");
        std::string name = args[0].as_string()->data;
        float cx = (args.size() >= 2) ? (float)args[1].to_double() : g_cam.x + g_cam.shake_ox;
        float cy = (args.size() >= 3) ? (float)args[2].to_double() : g_cam.y + g_cam.shake_oy;
        g_tiled.draw_all(name, cx, cy);
        return Value::make_none();
    });

    // TILED.DRAW_LAYER name$, layer$ [, cam_x, cam_y] — draw a specific layer
    vm.register_native("TILED.DRAW_LAYER", 2, 4, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TILED.DRAW_LAYER");
        std::string name = args[0].as_string()->data;
        std::string layer = args[1].as_string()->data;
        float cx = (args.size() >= 3) ? (float)args[2].to_double() : g_cam.x + g_cam.shake_ox;
        float cy = (args.size() >= 4) ? (float)args[3].to_double() : g_cam.y + g_cam.shake_oy;
        g_tiled.draw_layer(name, layer, cx, cy);
        return Value::make_none();
    });

    // TILED.LAYERS$(name$) → array of layer name strings (in draw order)
    vm.register_native("TILED.LAYERS$", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto names = g_tiled.get_layer_names(name);
        Value arr = Value::make_array();
        for (auto& n : names)
            arr.as_array()->elements.push_back(Value::make_string(n));
        return arr;
    });

    // TILED.OBJECTS(name$, layer$) → array of objects (each is an OBJECT with fields)
    vm.register_native("TILED.OBJECTS", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string layer = args[1].as_string()->data;
        auto objects = g_tiled.get_objects(name, layer);
        Value arr = Value::make_array();
        for (auto& obj : objects) {
            Value o = Value::make_object();
            auto* oo = o.as_object();
            oo->set("id", Value::make_i64(obj.id));
            oo->set("name", Value::make_string(obj.name));
            oo->set("type", Value::make_string(obj.type));
            oo->set("x", Value::make_f64(obj.x));
            oo->set("y", Value::make_f64(obj.y));
            oo->set("width", Value::make_f64(obj.width));
            oo->set("height", Value::make_f64(obj.height));
            oo->set("gid", Value::make_i64(obj.gid));
            // Custom properties as sub-object
            if (!obj.properties.empty()) {
                Value props = Value::make_object();
                auto* po = props.as_object();
                for (auto& [k, v] : obj.properties)
                    po->set(k, Value::make_string(v));
                oo->set("properties", props);
            }
            arr.as_array()->elements.push_back(o);
        }
        return arr;
    });

    // TILED.SIZE(name$) → [pixel_width, pixel_height]
    vm.register_native("TILED.SIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        int w, h;
        g_tiled.get_map_pixel_size(name, w, h);
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(w));
        arr.as_array()->elements.push_back(Value::make_i64(h));
        return arr;
    });

    // TILED.TILE_SIZE(name$) → [tile_w, tile_h]
    vm.register_native("TILED.TILE_SIZE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        int tw, th;
        g_tiled.get_tile_size(name, tw, th);
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(tw));
        arr.as_array()->elements.push_back(Value::make_i64(th));
        return arr;
    });

    // TILED.COLLIDES(sprite_id, name$, layer$) → bool
    vm.register_native("TILED.COLLIDES", 3, 3, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("TILED.COLLIDES", (int)args[0].to_int());
        std::string name = args[1].as_string()->data;
        std::string layer = args[2].as_string()->data;
        float sw = sp.w * sp.scale_x, sh = sp.h * sp.scale_y;
        return Value::make_bool(g_tiled.check_collision(name, layer, sp.x, sp.y, sw, sh));
    });

    // TILED.PROPERTIES(name$) → object with map-level custom properties
    vm.register_native("TILED.PROPERTIES", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        auto props = g_tiled.get_map_properties(name);
        Value obj = Value::make_object();
        auto* oo = obj.as_object();
        for (auto& [k, v] : props)
            oo->set(k, Value::make_string(v));
        return obj;
    });

    // TILED.TILE_AT(name$, layer$, pixel_x, pixel_y) → gid (0 = empty)
    vm.register_native("TILED.TILE_AT", 4, 4, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string layer = args[1].as_string()->data;
        float px = (float)args[2].to_double();
        float py = (float)args[3].to_double();
        return Value::make_i64(g_tiled.get_tile_at(name, layer, px, py));
    });
}
