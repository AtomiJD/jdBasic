// The sprite engine. Nearly all of it - motion, animation timing,
// collision, z-order, gravity - is arithmetic on the Sprite struct and
// runs anywhere. Six things need a platform underneath: reading an
// image, allocating one, uploading changed pixels, drawing, saving, and
// asking the time. Those sit behind the backend hooks below, with SDL on
// a desktop and the panel's own framebuffer on the board.
#if defined(GFX) || defined(PICO)
#include "vm.h"
#include "sprites.h"
#include "errors.h"
#ifdef GFX
#include "graphics_internal.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#endif
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

std::map<int, Sprite> g_sprites;
int g_next_sprite_id = 1;
static uint64_t g_last_update_tick = 0;

// Created sprites carry a CPU-side RGBA buffer. SETPIXEL / SETBUFFER
// mutate it and the backend uploads; SAVE reads from it, so nothing has
// to be read back off a screen.
struct SpritePixels {
    int width;
    int height;
    std::vector<uint8_t> rgba;   // width * height * 4, RGBA byte order
};

static std::map<int, SpritePixels> g_sprite_pixels;

// ── What a platform has to provide ───────────────────────────────────

static int      backend_image_create(int w, int h);
static void     backend_image_upload(int image_id, const SpritePixels& px);
static void     backend_draw(const Sprite& sp);
static uint64_t backend_ticks_ms();
// Loading and saving go through image files, which not every platform
// has a decoder for; these report whether they did anything.
static bool     backend_image_load(const std::string& path, int* image_id,
                                   float* w, float* h);
static bool     backend_image_save(const SpritePixels& px, const std::string& path);
// A board has no window to open; the desktop insists on one.
static void     backend_require_screen(const char* fn);
static std::string backend_asset_path(const std::string& given);

Sprite& get_sprite(const char* fn, int id) {
    auto it = g_sprites.find(id);
    if (it == g_sprites.end())
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": invalid sprite id " + std::to_string(id));
    return it->second;
}

#ifdef GFX

// Texture handle for a sprite id, for GUI.IMAGE (ImGui::Image). nullptr if the
// id or its texture is unknown; fills the pixel size when out_w/out_h given.
SDL_Texture* sprite_texture(int id, int* out_w, int* out_h) {
    auto it = g_sprites.find(id);
    if (it == g_sprites.end()) return nullptr;
    auto im = g_images.find(it->second.texture_id);
    if (im == g_images.end()) return nullptr;
    SDL_Texture* tex = im->second;
    if (out_w || out_h) {
        float w = 0.0f, h = 0.0f;
        SDL_GetTextureSize(tex, &w, &h);
        if (out_w) *out_w = (int)w;
        if (out_h) *out_h = (int)h;
    }
    return tex;
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

static int backend_image_create(int w, int h) {
    SDL_Texture* tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("SPRITE.CREATE: ") + SDL_GetError());
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    int id = g_next_image_id++;
    g_images[id] = tex;
    return id;
}

static void backend_image_upload(int image_id, const SpritePixels& px) {
    auto it = g_images.find(image_id);
    if (it == g_images.end()) return;
    SDL_UpdateTexture(it->second, nullptr, px.rgba.data(), px.width * 4);
}

static void backend_draw(const Sprite& sp) { draw_one_sprite(sp); }

static uint64_t backend_ticks_ms() { return (uint64_t)SDL_GetTicks(); }

static bool backend_image_load(const std::string& path, int* image_id,
                               float* w, float* h) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("SPRITE.LOAD: ") + SDL_GetError());
    *w = (float)surface->w;
    *h = (float)surface->h;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
    SDL_DestroySurface(surface);
    if (!tex)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("SPRITE.LOAD: ") + SDL_GetError());
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    *image_id = g_next_image_id++;
    g_images[*image_id] = tex;
    return true;
}

static bool backend_image_save(const SpritePixels& px, const std::string& path) {
    SDL_Surface* surf = SDL_CreateSurfaceFrom(px.width, px.height,
                                              SDL_PIXELFORMAT_RGBA32,
                                              (void*)px.rgba.data(),
                                              px.width * 4);
    if (!surf)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("SPRITE.SAVE: ") + SDL_GetError());
    bool ok = IMG_SavePNG(surf, path.c_str());
    SDL_DestroySurface(surf);
    if (!ok)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("SPRITE.SAVE: ") + SDL_GetError());
    return true;
}

static void backend_require_screen(const char* fn) { ensure_screen(fn); }

static std::string backend_asset_path(const std::string& given) {
    return resolve_asset_path(given);
}

#else   // PICO: the panel's own framebuffer, no window and no decoder

// An image is just its pixels here - there is nothing to upload them to.
static std::map<int, SpritePixels> g_pico_images;
static int g_pico_next_image = 1;

extern "C" void picocalc_gfx_blit_rgba(int dst_x, int dst_y,
                                       const uint8_t* rgba, int src_w,
                                       int sx, int sy, int w, int h,
                                       int flip_h, int flip_v, int alpha);
extern "C" uint32_t jdb_pico_millis(void);

static int backend_image_create(int w, int h) {
    int id = g_pico_next_image++;
    SpritePixels px;
    px.width = w;
    px.height = h;
    px.rgba.assign((size_t)w * h * 4, 0);
    g_pico_images[id] = std::move(px);
    return id;
}

static void backend_image_upload(int image_id, const SpritePixels& px) {
    g_pico_images[image_id] = px;
}

static void backend_draw(const Sprite& sp) {
    auto it = g_pico_images.find(sp.texture_id);
    if (it == g_pico_images.end()) return;
    const SpritePixels& px = it->second;
    int sx = 0, sy = 0;
    int w = (int)sp.w, h = (int)sp.h;
    if (sp.frame_w > 0 && sp.frame_h > 0) {
        int col = sp.cols > 0 ? sp.current_frame % sp.cols : 0;
        int row = sp.cols > 0 ? sp.current_frame / sp.cols : 0;
        sx = col * sp.frame_w;
        sy = row * sp.frame_h;
        w = sp.frame_w;
        h = sp.frame_h;
    }
    // Rotation and fractional scaling are what a GPU is for; the panel
    // gets position, frame, flip and alpha.
    picocalc_gfx_blit_rgba((int)sp.x, (int)sp.y, px.rgba.data(), px.width,
                           sx, sy, w, h, sp.flip_h ? 1 : 0, sp.flip_v ? 1 : 0,
                           sp.alpha);
}

static uint64_t backend_ticks_ms() { return (uint64_t)jdb_pico_millis(); }

static bool backend_image_load(const std::string&, int*, float*, float*) {
    throw jdError(ErrCode::RUNTIME_ERROR,
        "SPRITE.LOAD: no image decoder on this board - build sprites with "
        "SPRITE.CREATE and SPRITE.SETPIXEL/SETBUFFER");
}

static bool backend_image_save(const SpritePixels&, const std::string&) {
    throw jdError(ErrCode::RUNTIME_ERROR,
        "SPRITE.SAVE: no image encoder on this board");
}

static void backend_require_screen(const char*) {}

static std::string backend_asset_path(const std::string& given) { return given; }

// On a desktop graphics.cpp owns these and the CAMERA.* builtins move
// them; here the sprite engine is the only thing that has a view.
Camera g_cam;
int g_screen_w = 320;
int g_screen_h = 320;

#endif

void register_sprite_builtins(VM& vm) {
    // SPRITE.LOAD file$ - single image sprite
    // SPRITE.LOAD file$, frame_w, frame_h - spritesheet with frame size
    vm.register_native("SPRITE.LOAD", 1, 3, [](const std::vector<Value>& args) -> Value {
        backend_require_screen("SPRITE.LOAD");
        std::string path = backend_asset_path(args[0].as_string()->data);
        int img_id = 0;
        float tw = 0, th = 0;
        backend_image_load(path, &img_id, &tw, &th);

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

    // SPRITE.CREATE(w, h) - allocate a blank streaming texture + CPU pixel
    // buffer. Returns the new sprite id. Fill via SPRITE.SETPIXEL/SETBUFFER
    // and persist via SPRITE.SAVE.
    vm.register_native("SPRITE.CREATE", 2, 2, [](const std::vector<Value>& args) -> Value {
        backend_require_screen("SPRITE.CREATE");
        int w = (int)args[0].to_int();
        int h = (int)args[1].to_int();
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.CREATE: width/height must be 1..4096");

        int tid = backend_image_create(w, h);

        SpritePixels px;
        px.width = w;
        px.height = h;
        px.rgba.assign((size_t)w * h * 4, 0);   // transparent black
        backend_image_upload(tid, px);

        int sid = g_next_sprite_id++;
        Sprite sp{};
        sp.texture_id = tid;
        sp.x = 0; sp.y = 0;
        sp.vx = 0; sp.vy = 0;
        sp.scale_x = 1.0f; sp.scale_y = 1.0f;
        sp.origin_x = 0; sp.origin_y = 0;
        sp.angle = 0;
        sp.alpha = 255;
        sp.visible = true;
        sp.flip_h = false; sp.flip_v = false;
        sp.tex_w = (float)w; sp.tex_h = (float)h;
        sp.w = (float)w; sp.h = (float)h;
        sp.frame_w = 0; sp.frame_h = 0; sp.cols = 1;
        sp.current_frame = 0;
        sp.zorder = 0;
        sp.current_anim = -1;
        sp.anim_timer = 0; sp.playing = false;
        sp.gravity = 0; sp.on_ground = false;
        g_sprites[sid] = sp;

        g_sprite_pixels[sid] = std::move(px);
        return Value::make_i64(sid);
    });

    // SPRITE.SETPIXEL(id, x, y, r, g, b, a) - write one RGBA pixel.
    // Uploads the whole texture afterwards; convenient for incremental
    // edits, slow for bulk fills (use SETBUFFER for those).
    vm.register_native("SPRITE.SETPIXEL", 7, 7, [](const std::vector<Value>& args) -> Value {
        int sid = (int)args[0].to_int();
        auto it = g_sprite_pixels.find(sid);
        if (it == g_sprite_pixels.end())
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SETPIXEL: sprite " + std::to_string(sid) +
                " was not created via SPRITE.CREATE");
        SpritePixels& px = it->second;
        int x = (int)args[1].to_int();
        int y = (int)args[2].to_int();
        if (x < 0 || x >= px.width || y < 0 || y >= px.height)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SETPIXEL: pixel (" + std::to_string(x) + "," +
                std::to_string(y) + ") out of bounds for " +
                std::to_string(px.width) + "x" + std::to_string(px.height));
        size_t i = ((size_t)y * px.width + x) * 4;
        px.rgba[i + 0] = (uint8_t)args[3].to_int();
        px.rgba[i + 1] = (uint8_t)args[4].to_int();
        px.rgba[i + 2] = (uint8_t)args[5].to_int();
        px.rgba[i + 3] = (uint8_t)args[6].to_int();

        Sprite& sp = get_sprite("SPRITE.SETPIXEL", sid);
        backend_image_upload(sp.texture_id, px);
        return Value::make_none();
    });

    // SPRITE.SETBUFFER(id, rgba_array) - bulk fill from a flat RGBA array
    // of length width*height*4. Values clamped to 0..255.
    vm.register_native("SPRITE.SETBUFFER", 2, 2, [](const std::vector<Value>& args) -> Value {
        int sid = (int)args[0].to_int();
        auto it = g_sprite_pixels.find(sid);
        if (it == g_sprite_pixels.end())
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SETBUFFER: sprite " + std::to_string(sid) +
                " was not created via SPRITE.CREATE");
        SpritePixels& px = it->second;
        if (args[1].type != ValueType::ARRAY)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SETBUFFER: second arg must be a flat RGBA array");
        auto& elems = args[1].as_array()->elements;
        size_t expected = (size_t)px.width * px.height * 4;
        if (elems.size() != expected)
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SETBUFFER: array length " + std::to_string(elems.size()) +
                " does not match " + std::to_string(px.width) + "x" +
                std::to_string(px.height) + "x4 = " + std::to_string(expected));
        for (size_t i = 0; i < expected; ++i) {
            int v = (int)elems[i].to_int();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            px.rgba[i] = (uint8_t)v;
        }

        Sprite& sp = get_sprite("SPRITE.SETBUFFER", sid);
        backend_image_upload(sp.texture_id, px);
        return Value::make_none();
    });

    // SPRITE.SAVE(id, path$) - persist the CPU pixel buffer to a PNG.
    // Only works on sprites originally created via SPRITE.CREATE; for
    // SPRITE.LOAD'd images use GFX.SAVE_IMAGE instead.
    vm.register_native("SPRITE.SAVE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int sid = (int)args[0].to_int();
        auto it = g_sprite_pixels.find(sid);
        if (it == g_sprite_pixels.end())
            throw jdError(ErrCode::RUNTIME_ERROR,
                "SPRITE.SAVE: sprite " + std::to_string(sid) +
                " was not created via SPRITE.CREATE");
        SpritePixels& px = it->second;
        std::string path = backend_asset_path(args[1].as_string()->data);
        backend_image_save(px, path);
        return Value::make_none();
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
        backend_require_screen("SPRITE.DRAW");
        Sprite& sp = get_sprite("SPRITE.DRAW", (int)args[0].to_int());
        if (sp.visible) backend_draw(sp);
        return Value::make_none();
    });

    vm.register_native("SPRITE.DRAW_ALL", 0, 2, [](const std::vector<Value>& args) -> Value {
        backend_require_screen("SPRITE.DRAW_ALL");
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
            backend_draw(tmp);
        }
        return Value::make_none();
    });

    vm.register_native("SPRITE.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        g_sprites.erase(id);
        g_sprite_pixels.erase(id);
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

    // SPRITE.FRAME id, frame_index - manually set frame
    vm.register_native("SPRITE.FRAME", 2, 2, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.FRAME", (int)args[0].to_int());
        sp.current_frame = (int)args[1].to_int();
        sp.playing = false;
        sp.current_anim = -1;
        return Value::make_none();
    });

    // SPRITE.UPDATE - advance all animations by elapsed time
    vm.register_native("SPRITE.UPDATE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        uint64_t now = backend_ticks_ms();
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

#ifdef GFX
        // Particle update - the emitters live in graphics.cpp, which the
        // board does not build.
        for (auto it = g_particles.begin(); it != g_particles.end(); ) {
            it->life -= dt;
            if (it->life <= 0) { it = g_particles.erase(it); continue; }
            it->vy += it->gravity * dt;
            it->x += it->vx * dt;
            it->y += it->vy * dt;
            ++it;
        }
#endif

        return Value::make_none();
    });

    // SPRITE.PLAYING(id) -> bool - is the animation playing?
    vm.register_native("SPRITE.PLAYING", 1, 1, [](const std::vector<Value>& args) -> Value {
        Sprite& sp = get_sprite("SPRITE.PLAYING", (int)args[0].to_int());
        return Value::make_bool(sp.playing);
    });

    // ── Velocity System ───────────────────────────────────────

    // SPRITE.VELOCITY id, vx, vy - set velocity (pixels per second)
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

    // SPRITE.GROUP id, group_name$ - assign sprite to a group
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

    // SPRITE.LAND id, ground_y - snap to ground and zero vertical velocity
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
}
#endif
