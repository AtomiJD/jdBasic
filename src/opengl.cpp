#ifdef OPENGL
#include "vm.h"
#include "errors.h"
#include "opengl.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Defined in graphics.cpp. We share its SDL event queue so ON "KEYDOWN" /
// ON "QUIT" handlers fire from a GL-only loop too. POLLEVENT goes through
// an ImGui filter that eats keys; the gfx queue + VM event_poll() bypass it.
extern void gfx_push_event(const SDL_Event& ev);

// Resolves a script-relative asset path against g_base_dir (script's dir);
// matches the asset-lookup logic SPRITE.LOAD uses so GL.TEX.LOAD finds the
// same files via the same convention.
extern std::string resolve_asset_path(const std::string& p);

// ── Modern-GL bindings ─────────────────────────────────────────────────
// gl.h on Windows only exposes GL 1.1. For 3.3 we resolve everything
// through SDL_GL_GetProcAddress at runtime, after the context is current.

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER         0x8B31
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_INFO_LOG_LENGTH       0x8B84
#define GL_ARRAY_BUFFER          0x8892
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_STATIC_DRAW           0x88E4
#endif

// GL_LINES is in gl.h on Windows but spelled explicitly to keep enum
// reference local and avoid surprise if MSVC's gl.h ever drops it.
#ifndef GL_LINES
#define GL_LINES                 0x0001
#endif

// Texture unit enum (not in gl.h 1.1, GL_TEXTURE0 onwards is 1.3+).
#ifndef GL_TEXTURE0
#define GL_TEXTURE0              0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE         0x812F
#endif

typedef char       GLchar;
typedef ptrdiff_t  GLsizeiptr;
typedef ptrdiff_t  GLintptr;

typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void   (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void   (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY *PFN_glDeleteShader)(GLuint);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void   (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void   (APIENTRY *PFN_glUseProgram)(GLuint);
typedef void   (APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRY *PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void   (APIENTRY *PFN_glBindVertexArray)(GLuint);
typedef void   (APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void   (APIENTRY *PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef GLint  (APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void   (APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRY *PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY *PFN_glActiveTexture)(GLenum);

static PFN_glCreateShader            p_glCreateShader            = nullptr;
static PFN_glShaderSource            p_glShaderSource            = nullptr;
static PFN_glCompileShader           p_glCompileShader           = nullptr;
static PFN_glGetShaderiv             p_glGetShaderiv             = nullptr;
static PFN_glGetShaderInfoLog        p_glGetShaderInfoLog        = nullptr;
static PFN_glDeleteShader            p_glDeleteShader            = nullptr;
static PFN_glCreateProgram           p_glCreateProgram           = nullptr;
static PFN_glAttachShader            p_glAttachShader            = nullptr;
static PFN_glLinkProgram             p_glLinkProgram             = nullptr;
static PFN_glGetProgramiv            p_glGetProgramiv            = nullptr;
static PFN_glGetProgramInfoLog       p_glGetProgramInfoLog       = nullptr;
static PFN_glDeleteProgram           p_glDeleteProgram           = nullptr;
static PFN_glUseProgram              p_glUseProgram              = nullptr;
static PFN_glGenBuffers              p_glGenBuffers              = nullptr;
static PFN_glDeleteBuffers           p_glDeleteBuffers           = nullptr;
static PFN_glBindBuffer              p_glBindBuffer              = nullptr;
static PFN_glBufferData              p_glBufferData              = nullptr;
static PFN_glGenVertexArrays         p_glGenVertexArrays         = nullptr;
static PFN_glDeleteVertexArrays      p_glDeleteVertexArrays      = nullptr;
static PFN_glBindVertexArray         p_glBindVertexArray         = nullptr;
static PFN_glVertexAttribPointer     p_glVertexAttribPointer     = nullptr;
static PFN_glEnableVertexAttribArray p_glEnableVertexAttribArray = nullptr;
static PFN_glDrawArrays              p_glDrawArrays              = nullptr;
static PFN_glGetUniformLocation      p_glGetUniformLocation      = nullptr;
static PFN_glUniform1f               p_glUniform1f               = nullptr;
static PFN_glUniform1i               p_glUniform1i               = nullptr;
static PFN_glUniform3f               p_glUniform3f               = nullptr;
static PFN_glUniform4f               p_glUniform4f               = nullptr;
static PFN_glUniformMatrix4fv        p_glUniformMatrix4fv        = nullptr;
static PFN_glActiveTexture           p_glActiveTexture           = nullptr;

static bool g_gl_funcs_loaded = false;

static void load_gl_funcs() {
    if (g_gl_funcs_loaded) return;
    #define LOAD(name) \
        p_##name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
        if (!p_##name) throw jdError(ErrCode::RUNTIME_ERROR, \
            std::string("GL: failed to load ") + #name + " (need GL 3.3+ context)");
    LOAD(glCreateShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glDeleteShader);
    LOAD(glCreateProgram);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glDeleteProgram);
    LOAD(glUseProgram);
    LOAD(glGenBuffers);
    LOAD(glDeleteBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glGenVertexArrays);
    LOAD(glDeleteVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glVertexAttribPointer);
    LOAD(glEnableVertexAttribArray);
    LOAD(glDrawArrays);
    LOAD(glGetUniformLocation);
    LOAD(glUniform1f);
    LOAD(glUniform1i);
    LOAD(glUniform3f);
    LOAD(glUniform4f);
    LOAD(glUniformMatrix4fv);
    LOAD(glActiveTexture);
    #undef LOAD
    g_gl_funcs_loaded = true;
}

// Calling-convention macros to keep the rest of the file readable.
#define glCreateShader            p_glCreateShader
#define glShaderSource            p_glShaderSource
#define glCompileShader           p_glCompileShader
#define glGetShaderiv             p_glGetShaderiv
#define glGetShaderInfoLog        p_glGetShaderInfoLog
#define glDeleteShader            p_glDeleteShader
#define glCreateProgram           p_glCreateProgram
#define glAttachShader            p_glAttachShader
#define glLinkProgram             p_glLinkProgram
#define glGetProgramiv            p_glGetProgramiv
#define glGetProgramInfoLog       p_glGetProgramInfoLog
#define glDeleteProgram           p_glDeleteProgram
#define glUseProgram              p_glUseProgram
#define glGenBuffers              p_glGenBuffers
#define glDeleteBuffers           p_glDeleteBuffers
#define glBindBuffer              p_glBindBuffer
#define glBufferData              p_glBufferData
#define glGenVertexArrays         p_glGenVertexArrays
#define glDeleteVertexArrays      p_glDeleteVertexArrays
#define glBindVertexArray         p_glBindVertexArray
#define glVertexAttribPointer     p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDrawArrays              p_glDrawArrays
#define glGetUniformLocation      p_glGetUniformLocation
#define glUniform1f               p_glUniform1f
#define glUniform1i               p_glUniform1i
#define glUniform3f               p_glUniform3f
#define glUniform4f               p_glUniform4f
#define glUniformMatrix4fv        p_glUniformMatrix4fv
#define glActiveTexture           p_glActiveTexture

// ── Window/context state (Phase 1) ─────────────────────────────────────

static SDL_Window*   g_gl_window  = nullptr;
static SDL_GLContext g_gl_context = nullptr;

static void destroy_gl_window() {
    if (g_gl_context) {
        SDL_GL_DestroyContext(g_gl_context);
        g_gl_context = nullptr;
    }
    if (g_gl_window) {
        SDL_DestroyWindow(g_gl_window);
        g_gl_window = nullptr;
    }
    g_gl_funcs_loaded = false;
}

static void require_window(const char* fn) {
    if (!g_gl_window || !g_gl_context)
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": no GL window (call GL.WINDOW first)");
}

// Exposed so vm.cpp's event_poll() can drain the gfx event queue even when
// only a GL.WINDOW is open (no SCREEN -> no SDL_Renderer -> gfx_is_active=false).
bool gl_is_active() { return g_gl_window != nullptr && g_gl_context != nullptr; }

static const std::unordered_map<std::string, GLenum> kGlFlags = {
    { "DEPTH_TEST", GL_DEPTH_TEST },
    { "BLEND",      GL_BLEND      },
    { "CULL_FACE",  GL_CULL_FACE  },
};

static GLenum lookup_flag(const char* fn, const std::string& name) {
    auto it = kGlFlags.find(name);
    if (it == kGlFlags.end())
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": unknown GL flag '" + name +
            "' (known: DEPTH_TEST, BLEND, CULL_FACE)");
    return it->second;
}

// ── Mat4 helpers ───────────────────────────────────────────────────────
// Column-major (GL convention). m[col*4 + row] is the standard index.

static void mat4_identity(double m[16]) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1;
}

static void mat4_perspective(double m[16], double fov_rad, double aspect,
                             double zn, double zf) {
    double f = 1.0 / std::tan(fov_rad * 0.5);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0;
    m[14] = (2.0 * zf * zn) / (zn - zf);
}

static void mat4_lookat(double m[16],
                        double ex, double ey, double ez,
                        double tx, double ty, double tz,
                        double ux, double uy, double uz) {
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    double flen = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (flen > 0) { fx /= flen; fy /= flen; fz /= flen; }
    double sx = fy*uz - fz*uy;
    double sy = fz*ux - fx*uz;
    double sz = fx*uy - fy*ux;
    double slen = std::sqrt(sx*sx + sy*sy + sz*sz);
    if (slen > 0) { sx /= slen; sy /= slen; sz /= slen; }
    double upx = sy*fz - sz*fy;
    double upy = sz*fx - sx*fz;
    double upz = sx*fy - sy*fx;
    m[0] = sx;  m[4] = sy;  m[8]  = sz;  m[12] = -(sx*ex + sy*ey + sz*ez);
    m[1] = upx; m[5] = upy; m[9]  = upz; m[13] = -(upx*ex + upy*ey + upz*ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx*ex + fy*ey + fz*ez);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1;
}

static void mat4_mul(double out[16], const double a[16], const double b[16]) {
    double tmp[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[k*4 + r] * b[c*4 + k];
            tmp[c*4 + r] = s;
        }
    }
    for (int i = 0; i < 16; i++) out[i] = tmp[i];
}

static void mat4_translate(double out[16], const double m[16],
                           double tx, double ty, double tz) {
    double T[16]; mat4_identity(T);
    T[12] = tx; T[13] = ty; T[14] = tz;
    mat4_mul(out, m, T);
}

static void mat4_scale(double out[16], const double m[16],
                       double sx, double sy, double sz) {
    double S[16]; mat4_identity(S);
    S[0] = sx; S[5] = sy; S[10] = sz;
    mat4_mul(out, m, S);
}

static void mat4_rotate(double out[16], const double m[16],
                        double angle_rad, double ax, double ay, double az) {
    double len = std::sqrt(ax*ax + ay*ay + az*az);
    if (len == 0) { for (int i = 0; i < 16; i++) out[i] = m[i]; return; }
    ax /= len; ay /= len; az /= len;
    double c = std::cos(angle_rad), s = std::sin(angle_rad);
    double C = 1 - c;
    double R[16];
    R[0]  = c + ax*ax*C;     R[4] = ax*ay*C - az*s;  R[8]  = ax*az*C + ay*s;  R[12] = 0;
    R[1]  = ay*ax*C + az*s;  R[5] = c + ay*ay*C;     R[9]  = ay*az*C - ax*s;  R[13] = 0;
    R[2]  = az*ax*C - ay*s;  R[6] = az*ay*C + ax*s;  R[10] = c + az*az*C;     R[14] = 0;
    R[3]  = 0;               R[7] = 0;               R[11] = 0;               R[15] = 1;
    mat4_mul(out, m, R);
}

static Value mat4_to_value(const double m[16]) {
    std::vector<double> data(m, m + 16);
    return Value::make_tensor({16}, std::move(data));
}

static void mat4_from_value(const Value& v, const char* fn, double out[16]) {
    if (v.type == ValueType::TENSOR) {
        auto* t = v.as_tensor();
        if (t->data.size() != 16)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string(fn) + ": mat4 needs 16 elements, got " +
                std::to_string(t->data.size()));
        for (int i = 0; i < 16; i++) out[i] = t->data[i];
    } else if (v.type == ValueType::ARRAY) {
        auto& elems = v.as_array()->elements;
        if (elems.size() != 16)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string(fn) + ": mat4 needs 16 elements, got " +
                std::to_string(elems.size()));
        for (int i = 0; i < 16; i++) out[i] = elems[i].to_double();
    } else {
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string(fn) + ": mat4 must be a numeric array of 16 elements");
    }
}

// ── Shader helpers ─────────────────────────────────────────────────────

static GLuint compile_one(GLenum type, const std::string& src, const char* tag) {
    GLuint sh = glCreateShader(type);
    const GLchar* s = src.c_str();
    glShaderSource(sh, 1, &s, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log((size_t)std::max(1, len) + 1, '\0');
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        glDeleteShader(sh);
        throw jdError(ErrCode::RUNTIME_ERROR,
            std::string("GL.SHADER: ") + tag + " compile failed:\n" + log.data());
    }
    return sh;
}

void register_opengl_builtins(VM& vm) {
    // ── Window/context ─────────────────────────────────────────────────
    vm.register_native("GL.WINDOW", 2, 3, [](const std::vector<Value>& args) -> Value {
        int w = (int)args[0].to_int();
        int h = (int)args[1].to_int();
        std::string title = (args.size() >= 3 && args[2].type == ValueType::STRING)
            ? args[2].as_string()->data : "jdBasic GL";

        destroy_gl_window();

        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("GL.WINDOW: SDL_InitSubSystem failed: ") +
                    SDL_GetError());
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);

        g_gl_window = SDL_CreateWindow(title.c_str(), w, h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!g_gl_window)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.WINDOW: SDL_CreateWindow failed: ") +
                SDL_GetError());

        g_gl_context = SDL_GL_CreateContext(g_gl_window);
        if (!g_gl_context) {
            std::string err = SDL_GetError();
            SDL_DestroyWindow(g_gl_window);
            g_gl_window = nullptr;
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.WINDOW: SDL_GL_CreateContext failed: ") + err);
        }

        SDL_GL_MakeCurrent(g_gl_window, g_gl_context);
        SDL_GL_SetSwapInterval(1);
        glViewport(0, 0, w, h);

        SDL_RaiseWindow(g_gl_window);

        // Eagerly load modern-GL pointers so the first GL.SHADER call
        // doesn't pay the latency and so that fatal symbol-missing errors
        // surface at GL.WINDOW time, not deep in user code.
        load_gl_funcs();

        static bool atexit_set = false;
        if (!atexit_set) { std::atexit(destroy_gl_window); atexit_set = true; }

        return Value::make_none();
    });

    vm.register_native("GL.CLOSE", 0, 0, [](const std::vector<Value>&) -> Value {
        destroy_gl_window();
        return Value::make_none();
    });

    vm.register_native("GL.CLEAR", 3, 4, [](const std::vector<Value>& args) -> Value {
        require_window("GL.CLEAR");
        float r = (float)args[0].to_double() / 255.0f;
        float g = (float)args[1].to_double() / 255.0f;
        float b = (float)args[2].to_double() / 255.0f;
        float a = (args.size() >= 4) ? (float)args[3].to_double() / 255.0f : 1.0f;
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return Value::make_none();
    });

    vm.register_native("GL.FLIP", 0, 0, [](const std::vector<Value>&) -> Value {
        require_window("GL.FLIP");
        // Drain SDL events and forward to the gfx event queue so ON-handlers
        // (ON "KEYDOWN" / ON "QUIT") fire from a GL-only loop. Avoid
        // POLLEVENT — its ImGui filter swallows keys before they reach us.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            gfx_push_event(ev);
        }
        SDL_GL_SwapWindow(g_gl_window);
        return Value::make_none();
    });

    // GL.SAVE_SCREENSHOT(path$) - read the GL back buffer into a PNG. Call it
    // BEFORE GL.FLIP (after the swap the back buffer is undefined). glReadPixels
    // returns bottom-up rows, so the frame is flipped vertically before saving.
    vm.register_native("GL.SAVE_SCREENSHOT", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.SAVE_SCREENSHOT");
        std::string path = args[0].as_string()->data;
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(g_gl_window, &w, &h);
        if (w <= 0 || h <= 0)
            throw jdError(ErrCode::RUNTIME_ERROR, "GL.SAVE_SCREENSHOT: bad drawable size");
        std::vector<unsigned char> px((size_t)w * (size_t)h * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (!surf)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.SAVE_SCREENSHOT: ") + SDL_GetError());
        for (int y = 0; y < h; ++y) {  // flip: GL row 0 = bottom, PNG row 0 = top
            const unsigned char* src = px.data() + (size_t)(h - 1 - y) * (size_t)w * 4u;
            unsigned char* dst = (unsigned char*)surf->pixels + (size_t)y * (size_t)surf->pitch;
            memcpy(dst, src, (size_t)w * 4u);
        }
        bool ok = IMG_SavePNG(surf, path.c_str());
        SDL_DestroySurface(surf);
        if (!ok)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.SAVE_SCREENSHOT: ") + SDL_GetError());
        return Value::make_none();
    });

    vm.register_native("GL.VIEWPORT", 4, 4, [](const std::vector<Value>& args) -> Value {
        require_window("GL.VIEWPORT");
        glViewport((GLint)args[0].to_int(),  (GLint)args[1].to_int(),
                   (GLsizei)args[2].to_int(), (GLsizei)args[3].to_int());
        return Value::make_none();
    });

    vm.register_native("GL.ENABLE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.ENABLE");
        glEnable(lookup_flag("GL.ENABLE", args[0].as_string()->data));
        return Value::make_none();
    });

    vm.register_native("GL.DISABLE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.DISABLE");
        glDisable(lookup_flag("GL.DISABLE", args[0].as_string()->data));
        return Value::make_none();
    });

    // ── Phase 2: shaders, buffers, attribs, draw ───────────────────────

    vm.register_native("GL.SHADER", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.SHADER");
        std::string vs = args[0].as_string()->data;
        std::string fs = args[1].as_string()->data;
        GLuint vsh = compile_one(GL_VERTEX_SHADER,   vs, "vertex shader");
        GLuint fsh = compile_one(GL_FRAGMENT_SHADER, fs, "fragment shader");
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vsh);
        glAttachShader(prog, fsh);
        glLinkProgram(prog);
        glDeleteShader(vsh);
        glDeleteShader(fsh);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log((size_t)std::max(1, len) + 1, '\0');
            glGetProgramInfoLog(prog, len, nullptr, log.data());
            glDeleteProgram(prog);
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.SHADER: link failed:\n") + log.data());
        }
        return Value::make_i64((int64_t)prog);
    });

    vm.register_native("GL.USE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.USE");
        glUseProgram((GLuint)args[0].to_int());
        return Value::make_none();
    });

    vm.register_native("GL.SHADER.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.SHADER.DELETE");
        glDeleteProgram((GLuint)args[0].to_int());
        return Value::make_none();
    });

    // GL.VBO(data_array) — creates a vertex buffer, uploads the array as
    // floats, leaves it bound to GL_ARRAY_BUFFER. Returns the buffer id.
    // Accepts both TENSOR (typed numeric, the usual literal form) and
    // ARRAY (boxed mixed) to be forgiving.
    vm.register_native("GL.VBO", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.VBO");
        std::vector<float> data;
        if (args[0].type == ValueType::TENSOR) {
            auto* t = args[0].as_tensor();
            data.reserve(t->data.size());
            for (double d : t->data) data.push_back((float)d);
        } else if (args[0].type == ValueType::ARRAY) {
            auto& elems = args[0].as_array()->elements;
            data.reserve(elems.size());
            for (auto& v : elems) data.push_back((float)v.to_double());
        } else {
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.VBO: arg must be a numeric array (got type=") +
                std::to_string((int)args[0].type) + ")");
        }
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(data.size() * sizeof(float)),
                     data.data(), GL_STATIC_DRAW);
        return Value::make_i64((int64_t)vbo);
    });

    vm.register_native("GL.VBO.BIND", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.VBO.BIND");
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)args[0].to_int());
        return Value::make_none();
    });

    vm.register_native("GL.BUFFER.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.BUFFER.DELETE");
        GLuint b = (GLuint)args[0].to_int();
        glDeleteBuffers(1, &b);
        return Value::make_none();
    });

    // GL.VAO() — creates a VAO and leaves it bound.
    vm.register_native("GL.VAO", 0, 0, [](const std::vector<Value>&) -> Value {
        require_window("GL.VAO");
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        return Value::make_i64((int64_t)vao);
    });

    vm.register_native("GL.VAO.BIND", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.VAO.BIND");
        glBindVertexArray((GLuint)args[0].to_int());
        return Value::make_none();
    });

    vm.register_native("GL.VAO.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.VAO.DELETE");
        GLuint v = (GLuint)args[0].to_int();
        glDeleteVertexArrays(1, &v);
        return Value::make_none();
    });

    // GL.ATTRIB(idx, components, stride, offset)
    //   Assumes the VAO + VBO are currently bound. stride+offset are in
    //   bytes (use n * 4 for float-counts). type is GL_FLOAT, normalized=FALSE.
    vm.register_native("GL.ATTRIB", 4, 4, [](const std::vector<Value>& args) -> Value {
        require_window("GL.ATTRIB");
        GLuint  idx    = (GLuint)args[0].to_int();
        GLint   comps  = (GLint)args[1].to_int();
        GLsizei stride = (GLsizei)args[2].to_int();
        GLintptr off   = (GLintptr)args[3].to_int();
        glVertexAttribPointer(idx, comps, GL_FLOAT, GL_FALSE, stride,
                              (const void*)off);
        glEnableVertexAttribArray(idx);
        return Value::make_none();
    });

    // GL.DRAW.TRIS(first, count)
    vm.register_native("GL.DRAW.TRIS", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.DRAW.TRIS");
        glDrawArrays(GL_TRIANGLES, (GLint)args[0].to_int(),
                                   (GLsizei)args[1].to_int());
        return Value::make_none();
    });

    // ── Uniforms (subset, mat4 comes in P3) ───────────────────────────
    // Uniforms are keyed by program; we always look up against the
    // currently-USE'd program (queried via GL_CURRENT_PROGRAM). Caller
    // must GL.USE(prog) before setting uniforms. Unknown names silently
    // no-op so a hot-edited shader can drop a uniform without crashing.
    vm.register_native("GL.UNIFORM.F1", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.UNIFORM.F1");
        std::string name = args[0].as_string()->data;
        GLint cur_prog = 0;
        glGetIntegerv(0x8B8D /*GL_CURRENT_PROGRAM*/, &cur_prog);
        if (!cur_prog) return Value::make_none();
        GLint loc = glGetUniformLocation((GLuint)cur_prog, name.c_str());
        if (loc < 0) return Value::make_none();
        glUniform1f(loc, (GLfloat)args[1].to_double());
        return Value::make_none();
    });

    vm.register_native("GL.UNIFORM.F3", 4, 4, [](const std::vector<Value>& args) -> Value {
        require_window("GL.UNIFORM.F3");
        std::string name = args[0].as_string()->data;
        GLint cur_prog = 0;
        glGetIntegerv(0x8B8D, &cur_prog);
        if (!cur_prog) return Value::make_none();
        GLint loc = glGetUniformLocation((GLuint)cur_prog, name.c_str());
        if (loc < 0) return Value::make_none();
        glUniform3f(loc, (GLfloat)args[1].to_double(),
                          (GLfloat)args[2].to_double(),
                          (GLfloat)args[3].to_double());
        return Value::make_none();
    });

    vm.register_native("GL.UNIFORM.F4", 5, 5, [](const std::vector<Value>& args) -> Value {
        require_window("GL.UNIFORM.F4");
        std::string name = args[0].as_string()->data;
        GLint cur_prog = 0;
        glGetIntegerv(0x8B8D, &cur_prog);
        if (!cur_prog) return Value::make_none();
        GLint loc = glGetUniformLocation((GLuint)cur_prog, name.c_str());
        if (loc < 0) return Value::make_none();
        glUniform4f(loc, (GLfloat)args[1].to_double(),
                          (GLfloat)args[2].to_double(),
                          (GLfloat)args[3].to_double(),
                          (GLfloat)args[4].to_double());
        return Value::make_none();
    });

    vm.register_native("GL.UNIFORM.I1", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.UNIFORM.I1");
        std::string name = args[0].as_string()->data;
        GLint cur_prog = 0;
        glGetIntegerv(0x8B8D, &cur_prog);
        if (!cur_prog) return Value::make_none();
        GLint loc = glGetUniformLocation((GLuint)cur_prog, name.c_str());
        if (loc < 0) return Value::make_none();
        glUniform1i(loc, (GLint)args[1].to_int());
        return Value::make_none();
    });

    // ── Phase 3: mat4 stack + camera + line draw ───────────────────────

    vm.register_native("GL.UNIFORM.MAT4", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.UNIFORM.MAT4");
        std::string name = args[0].as_string()->data;
        double m[16];
        mat4_from_value(args[1], "GL.UNIFORM.MAT4", m);
        GLint cur_prog = 0;
        glGetIntegerv(0x8B8D, &cur_prog);
        if (!cur_prog) return Value::make_none();
        GLint loc = glGetUniformLocation((GLuint)cur_prog, name.c_str());
        if (loc < 0) return Value::make_none();
        float mf[16];
        for (int i = 0; i < 16; i++) mf[i] = (float)m[i];
        glUniformMatrix4fv(loc, 1, GL_FALSE, mf);     // column-major
        return Value::make_none();
    });

    vm.register_native("GL.DRAW.LINES", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.DRAW.LINES");
        glDrawArrays(GL_LINES, (GLint)args[0].to_int(),
                                (GLsizei)args[1].to_int());
        return Value::make_none();
    });

    // MAT4.IDENTITY() → 16-element flat tensor (column-major)
    vm.register_native("MAT4.IDENTITY", 0, 0, [](const std::vector<Value>&) -> Value {
        double m[16]; mat4_identity(m);
        return mat4_to_value(m);
    });

    // MAT4.PERSPECTIVE(fov_deg, aspect, near, far)
    vm.register_native("MAT4.PERSPECTIVE", 4, 4, [](const std::vector<Value>& args) -> Value {
        double fov_deg = args[0].to_double();
        double aspect  = args[1].to_double();
        double zn      = args[2].to_double();
        double zf      = args[3].to_double();
        double m[16];
        mat4_perspective(m, fov_deg * 3.14159265358979323846 / 180.0, aspect, zn, zf);
        return mat4_to_value(m);
    });

    // MAT4.LOOKAT(ex, ey, ez, tx, ty, tz, ux, uy, uz)
    vm.register_native("MAT4.LOOKAT", 9, 9, [](const std::vector<Value>& args) -> Value {
        double m[16];
        mat4_lookat(m,
            args[0].to_double(), args[1].to_double(), args[2].to_double(),
            args[3].to_double(), args[4].to_double(), args[5].to_double(),
            args[6].to_double(), args[7].to_double(), args[8].to_double());
        return mat4_to_value(m);
    });

    // MAT4.TRANSLATE(m, x, y, z) → m * T
    vm.register_native("MAT4.TRANSLATE", 4, 4, [](const std::vector<Value>& args) -> Value {
        double m[16], out[16];
        mat4_from_value(args[0], "MAT4.TRANSLATE", m);
        mat4_translate(out, m, args[1].to_double(), args[2].to_double(), args[3].to_double());
        return mat4_to_value(out);
    });

    // MAT4.ROTATE(m, angle_rad, ax, ay, az) → m * R
    vm.register_native("MAT4.ROTATE", 5, 5, [](const std::vector<Value>& args) -> Value {
        double m[16], out[16];
        mat4_from_value(args[0], "MAT4.ROTATE", m);
        mat4_rotate(out, m, args[1].to_double(),
                    args[2].to_double(), args[3].to_double(), args[4].to_double());
        return mat4_to_value(out);
    });

    // MAT4.SCALE(m, sx, sy, sz) → m * S
    vm.register_native("MAT4.SCALE", 4, 4, [](const std::vector<Value>& args) -> Value {
        double m[16], out[16];
        mat4_from_value(args[0], "MAT4.SCALE", m);
        mat4_scale(out, m, args[1].to_double(), args[2].to_double(), args[3].to_double());
        return mat4_to_value(out);
    });

    // MAT4.MUL(a, b) → a * b
    vm.register_native("MAT4.MUL", 2, 2, [](const std::vector<Value>& args) -> Value {
        double a[16], b[16], out[16];
        mat4_from_value(args[0], "MAT4.MUL", a);
        mat4_from_value(args[1], "MAT4.MUL", b);
        mat4_mul(out, a, b);
        return mat4_to_value(out);
    });

    // ── Phase 4: textures + element buffer + indexed draw ─────────────

    // GL.TEX.LOAD(path$) → texture handle. Loads PNG/JPG via SDL_image,
    // normalises to RGBA32, uploads as GL_RGBA, sets LINEAR+REPEAT defaults.
    // Path is resolved against the script directory (same rule as SPRITE.LOAD).
    vm.register_native("GL.TEX.LOAD", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.TEX.LOAD");
        std::string path = resolve_asset_path(args[0].as_string()->data);
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf)
            throw jdError(ErrCode::RUNTIME_ERROR,
                std::string("GL.TEX.LOAD: ") + SDL_GetError());
        // Normalise to RGBA32 so glTexImage2D sees a known byte order.
        SDL_Surface* rgba = surf;
        if (surf->format != SDL_PIXELFORMAT_RGBA32) {
            rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surf);
            if (!rgba)
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("GL.TEX.LOAD: SDL_ConvertSurface failed: ") +
                    SDL_GetError());
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     rgba->w, rgba->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

        SDL_DestroySurface(rgba);
        return Value::make_i64((int64_t)tex);
    });

    // GL.TEX.BIND(tex, slot) — slot is 0..N, mapped to GL_TEXTURE0+slot.
    vm.register_native("GL.TEX.BIND", 2, 2, [](const std::vector<Value>& args) -> Value {
        require_window("GL.TEX.BIND");
        GLuint tex  = (GLuint)args[0].to_int();
        GLint  slot = (GLint)args[1].to_int();
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, tex);
        return Value::make_none();
    });

    vm.register_native("GL.TEX.DELETE", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.TEX.DELETE");
        GLuint t = (GLuint)args[0].to_int();
        glDeleteTextures(1, &t);
        return Value::make_none();
    });

    // GL.EBO(indices) — uploads as GLuint indices, leaves bound to
    // GL_ELEMENT_ARRAY_BUFFER. The currently-bound VAO captures this binding
    // (one of the few non-attribute bits VAOs track), so subsequent
    // GL.DRAW.TRIS.IDX uses these indices when the VAO is bound.
    vm.register_native("GL.EBO", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.EBO");
        std::vector<GLuint> data;
        if (args[0].type == ValueType::TENSOR) {
            auto* t = args[0].as_tensor();
            data.reserve(t->data.size());
            for (double d : t->data) data.push_back((GLuint)(int64_t)d);
        } else if (args[0].type == ValueType::ARRAY) {
            auto& elems = args[0].as_array()->elements;
            data.reserve(elems.size());
            for (auto& v : elems) data.push_back((GLuint)v.to_int());
        } else {
            throw jdError(ErrCode::RUNTIME_ERROR,
                "GL.EBO: arg must be a numeric (integer) array");
        }
        GLuint ebo = 0;
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     (GLsizeiptr)(data.size() * sizeof(GLuint)),
                     data.data(), GL_STATIC_DRAW);
        return Value::make_i64((int64_t)ebo);
    });

    // GL.DRAW.TRIS.IDX(count) → glDrawElements with the bound EBO.
    // Offset is always 0 here; for sub-ranges use multiple EBOs.
    vm.register_native("GL.DRAW.TRIS.IDX", 1, 1, [](const std::vector<Value>& args) -> Value {
        require_window("GL.DRAW.TRIS.IDX");
        glDrawElements(GL_TRIANGLES, (GLsizei)args[0].to_int(),
                       GL_UNSIGNED_INT, nullptr);
        return Value::make_none();
    });
}

#endif // OPENGL
