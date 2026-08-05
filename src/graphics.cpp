#include "graphics.h"
#include "graphics_internal.h"
#include "sprites.h"
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
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <cmath>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
#else
  #include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Locate the directory containing the running jdBasic executable (or
// libjdbrt for native -c EXEs). Used to find jdbasic_default.ttf which
// ships next to the binary so SCREEN can auto-load a default font.
// Returns "" if the platform lookup fails (caller treats that as "no
// default font available" and reverts to the explicit-SETFONT path).
static std::string exe_directory() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string path((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, &path[0], len, nullptr, nullptr);
    auto pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#elif defined(__APPLE__)
    char buf[4096]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    std::string path(buf);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n <= 0) return "";
    std::string path(buf, (size_t)n);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#endif
}

// try_load_default_font is defined further down, after the file-scope
// font globals (g_font, g_ttf_init, g_font_path, g_font_size) - those
// are `static` so we cannot extern them from up here. Forward-declare
// just the signature so SCREEN / TEXT can call it.
static void try_load_default_font(float size_pt = 18.0f);

// Defined in main.cpp (exe) and vm_bridge.cpp (libjdbrt.so) - the
// directory of the .jdb the user invoked. Falls back to "." in the
// runtime DLL until the entry point sets it.
extern std::string g_base_dir;

// Resolve a user-supplied filesystem path: pass absolute paths through
// untouched, and for relative ones try CWD first (back-compat: most
// existing demos chdir before run), then fall back to a path relative
// to the main script's directory. Returns the path that actually exists,
// or the original input if neither did (so the caller still produces a
// recognisable error message).
//
// Declared in graphics_internal.h so sprites.cpp shares the same logic.
std::string resolve_asset_path(const std::string& p) {
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

SDL_Window*   g_window   = nullptr;
SDL_Renderer* g_renderer = nullptr;

#ifdef __EMSCRIPTEN__
// WebGL does not preserve the backbuffer across SDL_RenderPresent, so a
// draw/flip/draw/flip program would keep only the last batch. We render into
// this persistent target texture instead and blit it to the screen on
// SCREENFLIP, matching the desktop's accumulate-until-CLS model.
static SDL_Texture* g_screen_tex = nullptr;
#endif

// Streaming texture used by GFX.PLOT_POINTS_TEX. File-scope so it can be
// freed in cleanup_graphics() - otherwise the dangling pointer survives a
// SCREEN→GFX.CLOSE→SCREEN cycle and crashes the second run.
static SDL_Texture*       g_plot_tex   = nullptr;
static int                g_plot_tex_w = 0;
static int                g_plot_tex_h = 0;
static std::vector<uint32_t> g_plot_buf;

// Reusable scratch buffer for GFX.PLOT_POINTS. SDL3's renderer backends
// don't all copy the SDL_FPoint array on submit - a per-frame local
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

// Mouse coordinates are converted to the renderer's logical space here, at
// the single queue entry, so ON MOUSEDOWN/... handlers see the same
// coordinates on a scaled, letterboxed canvas as on a desktop window.
void gfx_push_event(const SDL_Event& ev) {
    SDL_Event copy = ev;
    if (g_renderer &&
        (copy.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         copy.type == SDL_EVENT_MOUSE_BUTTON_UP ||
         copy.type == SDL_EVENT_MOUSE_MOTION ||
         copy.type == SDL_EVENT_MOUSE_WHEEL)) {
        SDL_ConvertEventToRenderCoordinates(g_renderer, &copy);
    }
    g_pending_sdl_events.push_back(copy);
}
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
int                  g_screen_w = 0;
int                  g_screen_h = 0;
static float         g_scale    = 1.0f;

// Current draw color
Uint8 g_draw_r = 255, g_draw_g = 255, g_draw_b = 255, g_draw_a = 255;

// Current font
static TTF_Font* g_font = nullptr;
static std::string g_font_path;
static float g_font_size = 16.0f;
static bool g_ttf_init = false;

// Attempt to load the bundled default font (jdbasic_default.ttf, sits
// next to the EXE). NO-OP if a font is already loaded or the file is
// missing. Silent - callers that REQUIRE a font (TEXT) still throw their
// own "no font loaded" error when this falls through.
static void try_load_default_font(float size_pt) {
    if (g_font) return;
#ifdef __EMSCRIPTEN__
    // No executable path in the browser; the font is embedded at the FS root.
    std::string path = "/jdbasic_default.ttf";
#else
    std::string dir = exe_directory();
    if (dir.empty()) return;
    std::string path = dir + "/jdbasic_default.ttf";
#endif
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;
    if (!g_ttf_init) {
        if (!TTF_Init()) return;
        g_ttf_init = true;
    }
    g_font = TTF_OpenFont(path.c_str(), size_pt);
    if (g_font) {
        g_font_path = path;
        g_font_size = size_pt;
    }
}

// Audio state
static bool g_audio_init = false;
static MIX_Mixer* g_mixer = nullptr;

// Image cache: id -> texture (shared with sprites.cpp via graphics_internal.h)
int g_next_image_id = 1;
std::unordered_map<int, SDL_Texture*> g_images;

// ── Sprite state moved to src/sprites.cpp ───────────────────────

// ── Tilemap state ───────────────────────────────────────────────
struct Tilemap {
    int tileset_id;           // references g_images (spritesheet)
    int tile_w, tile_h;       // size of each tile in the tileset
    int tileset_cols;         // columns in the tileset image
    std::vector<std::vector<int>> data; // 2D grid of tile IDs (0 = empty)
    int rows, cols;           // map dimensions
};

static std::unordered_map<std::string, Tilemap> g_tilemaps;

// ── Camera + Particle state ─────────────────────────────────────
// Struct definitions and externs live in graphics_internal.h so
// sprites.cpp (SPRITE.UPDATE) can drive follow/shake/physics in step.
Camera g_cam;
std::vector<Particle> g_particles;

// ── Helpers ─────────────────────────────────────────────────────

// Declared in graphics_internal.h so sprites.cpp can share these.
void ensure_screen(const char* fn) {
    if (!g_renderer)
        throw jdError(ErrCode::RUNTIME_ERROR, std::string(fn) + ": no screen (call SCREEN first)");
}

void apply_draw_color() {
    SDL_SetRenderDrawColor(g_renderer, g_draw_r, g_draw_g, g_draw_b, g_draw_a);
}

// get_sprite + draw_one_sprite moved to src/sprites.cpp.

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

void gfx_shutdown() {
    cleanup_graphics();
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onScreenClose) Module.onScreenClose(); });
#endif
}

// Cross-thread resume signal. The REPL's RESUME command sets this from
// the main thread; the worker thread parked in gfx_console_pause_wait
// polls it. std::atomic so the worker doesn't need a mutex.
#include <atomic>
static std::atomic<bool> g_resume_signal{false};

void gfx_signal_resume() { g_resume_signal.store(true); }

// Pause-wait: keep the SDL window alive after a STOP_OP returned. Used
// in two scenarios:
//   * CLI mode (run_source): the script hit STOP, no MCP host to
//     issue jdb_resume; we wait for Space / Enter / F7 in the window.
//   * REPL mode (RUN worker): same thread that created the window now
//     parks here pumping events so the window stays responsive. The
//     REPL's RESUME command on the main thread sets g_resume_signal
//     to break us out.
// Returns:
//   true  -> Space / Enter / F7 pressed in window, OR resume signal
//            received from REPL (caller should call vm.resume()).
//   false -> user closed the window or pressed Esc (exit the program).
bool gfx_console_pause_wait() {
    if (!g_renderer) return false;
    g_resume_signal.store(false); // clear any stale signal on entry
    // Do NOT call SDL_RenderPresent here - SDL3's logical-presentation
    // back-buffer is cleared after present, so a bare re-present would
    // show a black frame and wipe out the script's overlay drawn just
    // before STOP. We rely on the OS to keep the last presented frame
    // visible while we pump events.
    for (;;) {
        if (g_resume_signal.load()) {
            g_resume_signal.store(false);
            return true;
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) return false;
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                SDL_Scancode sc = ev.key.scancode;
                if (sc == SDL_SCANCODE_SPACE  ||
                    sc == SDL_SCANCODE_RETURN ||
                    sc == SDL_SCANCODE_F7) {
                    return true;
                }
                if (sc == SDL_SCANCODE_ESCAPE) return false;
            }
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
        }
        SDL_Delay(16);
    }
}

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

#ifdef __EMSCRIPTEN__
    if (g_screen_tex) {
        if (g_renderer) SDL_SetRenderTarget(g_renderer, nullptr);
        SDL_DestroyTexture(g_screen_tex);
        g_screen_tex = nullptr;
    }
#endif

    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }
    if (g_sdl_init) { SDL_Quit(); g_sdl_init = false; }
}

// ── Register all graphics/audio builtins ────────────────────────

void register_graphics_builtins(VM& vm) {

    // Sprite subsystem owns its own builtins now (see src/sprites.cpp).
    register_sprite_builtins(vm);

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
        // to the freed renderer - the new one ends up with
        // PRESENTATION_DISABLED and draws 1:1 in the top-left.
        gui_shutdown();
#endif
#ifdef __EMSCRIPTEN__
        if (g_screen_tex) {
            if (g_renderer) SDL_SetRenderTarget(g_renderer, nullptr);
            SDL_DestroyTexture(g_screen_tex);
            g_screen_tex = nullptr;
        }
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

#ifndef __EMSCRIPTEN__
        // Clamp to display size to avoid unintended fullscreen. Desktop only:
        // in the browser the canvas carries no window chrome to make room for,
        // and shrinking it by the desktop margins breaks its aspect ratio -
        // the page then scales a letterboxed picture instead of the picture.
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(display);
        if (dm) {
            if (win_w > dm->w) win_w = dm->w - 40;
            if (win_h > dm->h) win_h = dm->h - 80;
        }
#endif

#ifdef __EMSCRIPTEN__
        // Reveal + size the page canvas BEFORE the window/GL context is created,
        // so the GL context binds to a visible, correctly-sized canvas.
        EM_ASM({ if (Module.onScreenOpen) Module.onScreenOpen($0, $1); }, win_w, win_h);
#endif

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

#ifdef __EMSCRIPTEN__
        // Persistent backbuffer: draw into a logical-size target texture; the
        // demos draw in logical coordinates, which map 1:1 to it. SCREENFLIP
        // blits it to the (logically-scaled) window.
        g_screen_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, w, h);
        if (g_screen_tex) {
            SDL_SetRenderTarget(g_renderer, g_screen_tex);
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderClear(g_renderer);
        }
#endif

        // Bring window to front and give it focus
        SDL_RaiseWindow(g_window);

#ifdef IMGUI
        gui_init(g_window, g_renderer, g_scale);
#endif

        // Init TiledMap system with the renderer
        g_tiled.init(g_renderer);

        // Auto-load bundled default font (silent skip if missing or
        // if a font is already loaded). Lets demos use TEXT without
        // having to ship SETFONT boilerplate.
        try_load_default_font();

        // Register cleanup at exit
        static bool atexit_set = false;
        if (!atexit_set) { std::atexit(cleanup_graphics); atexit_set = true; }

        return Value::make_none();
    });

    // ── SCREENFLIP ──────────────────────────────────────────────

    // ── SCREENWIDTH() / SCREENHEIGHT() - query the logical SCREEN size ──
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
#ifdef __EMSCRIPTEN__
        // Blit the persistent target texture to the window, then restore it as
        // the draw target so the next frame keeps accumulating on it. Clear
        // with black, not the program's current draw color - the clear color
        // shows in the logical-presentation letterbox bars on a scaled canvas.
        if (g_screen_tex) {
            SDL_SetRenderTarget(g_renderer, nullptr);
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderClear(g_renderer);
            SDL_RenderTexture(g_renderer, g_screen_tex, nullptr, nullptr);
        }
#endif
        SDL_RenderPresent(g_renderer);
#ifdef __EMSCRIPTEN__
        if (g_screen_tex) {
            SDL_SetRenderTarget(g_renderer, g_screen_tex);
            apply_draw_color();
        }
        // Hand a turn to the browser so it composites the freshly presented
        // frame to the canvas. A tight draw/SCREENFLIP loop with no SLEEP would
        // otherwise never let the page paint or deliver input.
        emscripten_sleep(0);
#endif
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
            // match it - SDL_SetRenderLogicalPresentation handles the upscale
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
            // RECT x, y, w, h, [fill], [r, g, b] - fill before colour (matches the
            // docs and the matrix path; the reverse order reads the fill flag as red).
            float x = (float)args[0].to_double(), y = (float)args[1].to_double();
            float w = (float)args[2].to_double(), h = (float)args[3].to_double();
            bool fill = (args.size() >= 5) ? args[4].to_bool() : false;
            Uint8 r, g, b;
            bool has = extract_rgb(args, 5, r, g, b);
            ColorGuard cg(has, r, g, b);
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
            // CIRCLE cx, cy, r, [fill], [r, g, b]
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float cr = (float)args[2].to_double();
            bool fill = (args.size() >= 4) ? args[3].to_bool() : false;
            Uint8 r, g, b;
            bool has = extract_rgb(args, 4, r, g, b);
            ColorGuard cg(has, r, g, b);
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
            // ELLIPSE cx, cy, rx, ry, [fill], [r, g, b]
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float rx = (float)args[2].to_double();
            float ry = (float)args[3].to_double();
            bool fill = (args.size() >= 5) ? args[4].to_bool() : false;
            Uint8 r, g, b;
            bool has = extract_rgb(args, 5, r, g, b);
            ColorGuard cg(has, r, g, b);
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
            // ROUNDED_RECT x, y, w, h, radius, [fill], [r, g, b]
            float x = (float)args[0].to_double();
            float y = (float)args[1].to_double();
            float w = (float)args[2].to_double();
            float h = (float)args[3].to_double();
            float rad = (float)args[4].to_double();
            bool fill = (args.size() >= 6) ? args[5].to_bool() : false;
            Uint8 r, g, b;
            bool has = extract_rgb(args, 6, r, g, b);
            ColorGuard cg(has, r, g, b);
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
            // CIRCLE_SECTOR cx, cy, radius, start_angle, end_angle, [fill], [r, g, b]
            // fill comes before the colour, matching CIRCLE and the documented
            // signature. The old order grabbed the fill flag as the red channel
            // and left fill=false, so a filled sector rendered as a near-invisible
            // outline.
            float cx = (float)args[0].to_double();
            float cy = (float)args[1].to_double();
            float rad = (float)args[2].to_double();
            float sa = (float)args[3].to_double();
            float ea = (float)args[4].to_double();
            bool fill = (args.size() >= 6) ? args[5].to_bool() : false;
            Uint8 r, g, b;
            bool has = extract_rgb(args, 6, r, g, b);
            ColorGuard cg(has, r, g, b);
            if (fill) draw_sector_filled(cx, cy, rad, sa, ea);
            else draw_sector_outline(cx, cy, rad, sa, ea);
        }
        return Value::make_none();
    });

    // ── TEXT x, y, content$, [r, g, b] ──────────────────────────

    vm.register_native("TEXT", 3, 6, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TEXT");
        if (!g_font) try_load_default_font();
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
                // use - NOT just because ImGui-Nav has implicit keyboard
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

    // Mouse coordinates come from SDL in window space; convert to the
    // renderer's logical space so a CSS-scaled, letterboxed canvas (the
    // browser build on a phone) reports the same coordinates as a desktop
    // window that matches the logical size exactly.
    vm.register_native("GFX.MOUSEX", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float x, y;
        SDL_GetMouseState(&x, &y);
        if (g_renderer) {
            float lx, ly;
            SDL_RenderCoordinatesFromWindow(g_renderer, x, y, &lx, &ly);
            return Value::make_i64((int64_t)lx);
        }
        return Value::make_i64((int64_t)x);
    });

    vm.register_native("GFX.MOUSEY", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        float x, y;
        SDL_GetMouseState(&x, &y);
        if (g_renderer) {
            float lx, ly;
            SDL_RenderCoordinatesFromWindow(g_renderer, x, y, &lx, &ly);
            return Value::make_i64((int64_t)ly);
        }
        return Value::make_i64((int64_t)y);
    });

    vm.register_native("GFX.MOUSEBUTTON", 0, 1, [](const std::vector<Value>& args) -> Value {
        int btn = args.empty() ? 1 : (int)args[0].to_int();
        float x, y;
        SDL_MouseButtonFlags state = SDL_GetMouseState(&x, &y);
        return Value::make_bool((state & SDL_BUTTON_MASK(btn)) != 0);
    });

    vm.register_native("GFX.DELAY", 1, 1, [](const std::vector<Value>& args) -> Value {
#ifdef __EMSCRIPTEN__
        // SDL_Delay busy-waits without handing control back to the browser;
        // emscripten_sleep yields so the page composites and stays responsive.
        emscripten_sleep((unsigned)args[0].to_int());
#else
        SDL_Delay((Uint32)args[0].to_int());
#endif
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

    // PARTICLE.DRAW [cam_x, cam_y] - draw all particles
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

    // PARTICLE.CLEAR - remove all particles
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

    // GFX.SAVE_SCREENSHOT file$, [x, y, w, h] - save full screen or a region
    vm.register_native("GFX.SAVE_SCREENSHOT", 1, 5, [](const std::vector<Value>& args) -> Value {
        ensure_screen("GFX.SAVE_SCREENSHOT");
        std::string path = resolve_asset_path(args[0].as_string()->data);

        SDL_Rect region_rect;
        SDL_Rect* rect_ptr = nullptr;
        if (args.size() >= 5) {
            region_rect.x = (int)args[1].to_int();
            region_rect.y = (int)args[2].to_int();
            region_rect.w = (int)args[3].to_int();
            region_rect.h = (int)args[4].to_int();
            rect_ptr = &region_rect;
        } else {
            // Default to the current renderer viewport instead of NULL.
            // SDL3's D3D11 backend has a broken staging-texture path for
            // a NULL rect under some configs; the viewport rect always
            // matches the logical presentation and reads cleanly.
            if (SDL_GetRenderViewport(g_renderer, &region_rect)) {
                rect_ptr = &region_rect;
            }
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
    // ── Audio (SDL3_mixer 3.4+ - track/audio model) ──────────────
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
    // channels - that collapses to a single track per id that restarts
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
    // as Mix_PlayMusic semantics).
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
            // handles WITHOUT closing - SDL_Quit already did that -
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

    // TILED.UPDATE(dt) - update tile animations (dt in seconds)
    vm.register_native("TILED.UPDATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_tiled.update((float)args[0].to_double());
        return Value::make_none();
    });

    // TILED.DRAW name$ [, cam_x, cam_y] - draw all layers in order
    vm.register_native("TILED.DRAW", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_screen("TILED.DRAW");
        std::string name = args[0].as_string()->data;
        float cx = (args.size() >= 2) ? (float)args[1].to_double() : g_cam.x + g_cam.shake_ox;
        float cy = (args.size() >= 3) ? (float)args[2].to_double() : g_cam.y + g_cam.shake_oy;
        g_tiled.draw_all(name, cx, cy);
        return Value::make_none();
    });

    // TILED.DRAW_LAYER name$, layer$ [, cam_x, cam_y] - draw a specific layer
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
