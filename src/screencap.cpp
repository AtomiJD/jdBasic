// screencap.cpp - backing for the OS.SCREENSHOT builtin.
//
// Windows: GDI BitBlt capture + WIC encode. The output format (PNG / JPG /
// BMP / TIFF / GIF) is chosen from the file extension; WIC converts the
// captured 32bpp BGRA frame to whatever the container needs (e.g. 24bpp for
// JPEG) via WriteSource. Other platforms return SC_UNSUPPORTED.
//
// This translation unit is linked into BOTH the interpreter EXE and the
// native runtime DLL, so OS.SCREENSHOT works in interp and in `-c` builds
// (the native path reaches it through the VM bridge).
//
// rc: 0 = ok, negative = error.

#include <string>
#include <cstdint>
#include <cctype>

enum {
    SC_OK          = 0,
    SC_E_PATH      = -1,
    SC_E_WINDOW    = -2,
    SC_E_CAPTURE   = -3,
    SC_E_ENCODE    = -4,
    SC_E_SIZE      = -5,
    SC_UNSUPPORTED = -100
};

#ifdef _WIN32

#include <windows.h>
#include <wincodec.h>
#include <vector>

static GUID sc_container_for_ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == "jpg" || ext == "jpeg") return GUID_ContainerFormatJpeg;
    if (ext == "bmp")                  return GUID_ContainerFormatBmp;
    if (ext == "tif" || ext == "tiff") return GUID_ContainerFormatTiff;
    if (ext == "gif")                  return GUID_ContainerFormatGif;
    return GUID_ContainerFormatPng;  // default + .png
}

// Encode a top-down 32bpp BGRA buffer to `wpath` in the given container.
static int sc_wic_write(const wchar_t* wpath, const uint8_t* bgra,
                        int w, int h, const GUID& container) {
    bool did_co = false;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) did_co = true;  // else: already init elsewhere

    IWICImagingFactory*    factory = NULL;
    IWICBitmap*            bitmap  = NULL;
    IWICStream*            stream  = NULL;
    IWICBitmapEncoder*     encoder = NULL;
    IWICBitmapFrameEncode* frame   = NULL;
    IPropertyBag2*         props   = NULL;
    int rc = SC_E_ENCODE;

    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) goto done;

    hr = factory->CreateBitmapFromMemory((UINT)w, (UINT)h,
            GUID_WICPixelFormat32bppBGRA, (UINT)(w * 4),
            (UINT)((size_t)w * 4u * (size_t)h), (BYTE*)bgra, &bitmap);
    if (FAILED(hr)) goto done;

    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) goto done;
    hr = stream->InitializeFromFilename(wpath, GENERIC_WRITE);
    if (FAILED(hr)) { rc = SC_E_PATH; goto done; }

    hr = factory->CreateEncoder(container, NULL, &encoder);
    if (FAILED(hr)) goto done;
    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) goto done;

    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) goto done;
    hr = frame->Initialize(props);
    if (FAILED(hr)) goto done;
    frame->SetSize((UINT)w, (UINT)h);
    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&fmt);
    }
    hr = frame->WriteSource(bitmap, NULL);   // WIC converts to the container fmt
    if (FAILED(hr)) goto done;
    if (FAILED(frame->Commit()))   goto done;
    if (FAILED(encoder->Commit())) goto done;
    rc = SC_OK;

done:
    if (props)   props->Release();
    if (frame)   frame->Release();
    if (encoder) encoder->Release();
    if (stream)  stream->Release();
    if (bitmap)  bitmap->Release();
    if (factory) factory->Release();
    if (did_co)  CoUninitialize();
    return rc;
}

extern "C" int jdb_screencap(const char* path_utf8, const char* mode,
                             const char* caption) {
    if (!path_utf8 || !*path_utf8) return SC_E_PATH;
    std::string m = mode ? mode : "screen";
    for (auto& c : m) c = (char)tolower((unsigned char)c);

    bool full   = m.empty() || m == "screen" || m == "fullscreen" || m == "full";
    bool client = (m == "client") || (m == "content") || (m == "windowcontent");
    // any other non-full mode => whole window frame (title bar + borders)

    int x = 0, y = 0, w = 0, h = 0;
    if (full) {
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (w <= 0 || h <= 0) {  // fall back to the primary monitor
            x = 0; y = 0;
            w = GetSystemMetrics(SM_CXSCREEN);
            h = GetSystemMetrics(SM_CYSCREEN);
        }
    } else {
        HWND target = (caption && *caption) ? FindWindowA(NULL, caption)
                                            : GetForegroundWindow();
        if (!target || !IsWindow(target)) return SC_E_WINDOW;
        RECT r;
        if (client) {
            if (!GetClientRect(target, &r)) return SC_E_WINDOW;
            POINT o = { 0, 0 };
            ClientToScreen(target, &o);
            x = o.x; y = o.y; w = r.right - r.left; h = r.bottom - r.top;
        } else {
            if (!GetWindowRect(target, &r)) return SC_E_WINDOW;
            x = r.left; y = r.top; w = r.right - r.left; h = r.bottom - r.top;
        }
    }
    if (w <= 0 || h <= 0) return SC_E_SIZE;

    HDC screen = GetDC(NULL);
    if (!screen) return SC_E_CAPTURE;
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = mem ? CreateCompatibleBitmap(screen, w, h) : NULL;
    int rc = SC_E_CAPTURE;
    if (mem && bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        if (BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY)) {
            BITMAPINFO bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth       = w;
            bi.bmiHeader.biHeight      = -h;   // negative => top-down rows
            bi.bmiHeader.biPlanes      = 1;
            bi.bmiHeader.biBitCount    = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            std::vector<uint8_t> buf((size_t)w * (size_t)h * 4u);
            if (GetDIBits(mem, bmp, 0, (UINT)h, buf.data(), &bi, DIB_RGB_COLORS)) {
                int wn = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
                std::wstring wpath;
                if (wn > 0) {
                    wpath.resize((size_t)wn - 1);
                    MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, &wpath[0], wn);
                }
                rc = sc_wic_write(wpath.c_str(), buf.data(), w, h,
                                  sc_container_for_ext(path_utf8));
            }
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return rc;
}

#elif defined(__linux__) && defined(GFX)

// Linux (X11 / XWayland): XGetImage capture + SDL3_image encode. Format is
// chosen from the extension (.jpg/.jpeg -> JPEG, else PNG). XWayland is
// rootless, so "screen" mode only sees the X root (empty on a pure-Wayland
// desktop); capturing a window by caption or the active window is the reliable
// path, and jdBasic's own SDL windows run under XWayland so they read cleanly.

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>

static int sc_ctz(unsigned long m) { int n = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; n++; } return n; }
static int sc_bits(unsigned long m) { int n = 0; while (m) { n += (int)(m & 1); m >>= 1; } return n; }

// True if window `w`'s title (WM_NAME or _NET_WM_NAME) contains `needle`.
static bool sc_title_matches(Display* dpy, Window w, const char* needle) {
    char* name = nullptr;
    if (XFetchName(dpy, w, &name) && name) {
        bool hit = strstr(name, needle) != nullptr;
        XFree(name);
        if (hit) return true;
    }
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    if (net_name != None && utf8 != None) {
        Atom type; int fmt; unsigned long n, after; unsigned char* data = nullptr;
        if (XGetWindowProperty(dpy, w, net_name, 0, 1024, False, utf8,
                               &type, &fmt, &n, &after, &data) == Success && data) {
            bool hit = strstr((const char*)data, needle) != nullptr;
            XFree(data);
            if (hit) return true;
        }
    }
    return false;
}

// Depth-first search of the window tree for the first title match. This lands
// on the WM frame (title bar + borders + shadow) because a reparenting WM
// copies the title onto the frame, which sits above the client.
static Window sc_find_window(Display* dpy, Window w, const char* needle) {
    if (sc_title_matches(dpy, w, needle)) return w;
    Window root, parent, *children = nullptr; unsigned int count = 0;
    Window found = 0;
    if (XQueryTree(dpy, w, &root, &parent, &children, &count)) {
        for (unsigned int i = 0; i < count && !found; i++)
            found = sc_find_window(dpy, children[i], needle);
        if (children) XFree(children);
    }
    return found;
}

// From a matched frame, descend to the deepest descendant that still carries
// the title - the actual client (content) window - for "client"/"content" mode.
static Window sc_deepest_match(Display* dpy, Window w, const char* needle) {
    Window best = w;
    Window root, parent, *children = nullptr; unsigned int count = 0;
    if (XQueryTree(dpy, w, &root, &parent, &children, &count)) {
        for (unsigned int i = 0; i < count; i++) {
            if (sc_title_matches(dpy, children[i], needle))
                best = sc_deepest_match(dpy, children[i], needle);
        }
        if (children) XFree(children);
    }
    return best;
}

static Window sc_active_window(Display* dpy, Window root) {
    Atom prop = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    if (prop == None) return 0;
    Atom type; int fmt; unsigned long n, after; unsigned char* data = nullptr;
    Window win = 0;
    if (XGetWindowProperty(dpy, root, prop, 0, 1, False, AnyPropertyType,
                           &type, &fmt, &n, &after, &data) == Success) {
        if (data && n >= 1) win = *(Window*)data;
        if (data) XFree(data);
    }
    return win;
}

extern "C" int jdb_screencap(const char* path_utf8, const char* mode,
                             const char* caption) {
    if (!path_utf8 || !*path_utf8) return SC_E_PATH;
    std::string m = mode ? mode : "screen";
    for (auto& c : m) c = (char)tolower((unsigned char)c);
    bool full = m.empty() || m == "screen" || m == "fullscreen" || m == "full";
    bool client = (m == "client") || (m == "content") || (m == "windowcontent");

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return SC_E_CAPTURE;

    Window root = DefaultRootWindow(dpy);
    Window target;
    if (full) {
        target = root;
    } else {
        target = (caption && *caption) ? sc_find_window(dpy, root, caption)
                                       : sc_active_window(dpy, root);
        if (!target) { XCloseDisplay(dpy); return SC_E_WINDOW; }
        // "client" trims the WM frame by dropping to the content window.
        if (client && caption && *caption)
            target = sc_deepest_match(dpy, target, caption);
    }

    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, target, &wa) || wa.width <= 0 || wa.height <= 0) {
        XCloseDisplay(dpy); return SC_E_SIZE;
    }
    int w = wa.width, h = wa.height;

    XImage* img = XGetImage(dpy, target, 0, 0, w, h, AllPlanes, ZPixmap);
    if (!img) { XCloseDisplay(dpy); return SC_E_CAPTURE; }

    int rc = SC_E_ENCODE;
    SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        unsigned long rmask = img->red_mask, gmask = img->green_mask, bmask = img->blue_mask;
        int rsh = sc_ctz(rmask), gsh = sc_ctz(gmask), bsh = sc_ctz(bmask);
        int rbits = sc_bits(rmask), gbits = sc_bits(gmask), bbits = sc_bits(bmask);
        uint8_t* base = (uint8_t*)surf->pixels;
        for (int py = 0; py < h; py++) {
            uint8_t* row = base + (size_t)py * (size_t)surf->pitch;
            for (int px = 0; px < w; px++) {
                unsigned long p = XGetPixel(img, px, py);
                unsigned long r = (p & rmask) >> rsh;
                unsigned long g = (p & gmask) >> gsh;
                unsigned long b = (p & bmask) >> bsh;
                if (rbits && rbits != 8) r = r * 255ul / ((1ul << rbits) - 1);
                if (gbits && gbits != 8) g = g * 255ul / ((1ul << gbits) - 1);
                if (bbits && bbits != 8) b = b * 255ul / ((1ul << bbits) - 1);
                uint8_t* q = row + (size_t)px * 4;   // RGBA32: bytes R,G,B,A
                q[0] = (uint8_t)r; q[1] = (uint8_t)g; q[2] = (uint8_t)b; q[3] = 255;
            }
        }
        std::string ext;
        size_t dot = std::string(path_utf8).find_last_of('.');
        if (dot != std::string::npos) {
            ext = std::string(path_utf8).substr(dot + 1);
            for (auto& c : ext) c = (char)tolower((unsigned char)c);
        }
        bool ok = (ext == "jpg" || ext == "jpeg") ? IMG_SaveJPG(surf, path_utf8, 90)
                                                   : IMG_SavePNG(surf, path_utf8);
        rc = ok ? SC_OK : SC_E_PATH;
        SDL_DestroySurface(surf);
    }
    XDestroyImage(img);
    XCloseDisplay(dpy);
    return rc;
}

#else  // other platforms / non-GFX builds: capture not implemented

extern "C" int jdb_screencap(const char*, const char*, const char*) {
    return SC_UNSUPPORTED;
}

#endif
