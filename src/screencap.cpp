// screencap.cpp - backing for OS.SCREENSHOT and the pixel side of IMAGE.*
// (CONVERT, CROP, RESIZE).
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
#include <cstring>
#include "image_meta.h"

enum {
    SC_OK          = 0,
    SC_E_PATH      = -1,
    SC_E_WINDOW    = -2,
    SC_E_RECT      = -2,   // IMAGE.CROP / IMAGE.RESIZE: a rectangle or size out of range
    SC_E_CAPTURE   = -3,
    SC_E_ENCODE    = -4,
    SC_E_SIZE      = -5,
    SC_E_DECODE    = -6,
    SC_UNSUPPORTED = -100
};

static const unsigned long long SC_MAX_PIXELS = 100000000ull;

#ifdef _WIN32

#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <new>

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

static std::wstring sc_wide(const char* utf8) {
    std::wstring w;
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wn > 0) {
        w.resize((size_t)wn - 1);
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], wn);
    }
    return w;
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
        if (IsEqualGUID(fmt, GUID_WICPixelFormat8bppIndexed)) {
            // An indexed container (GIF) takes a palette made from the pixels.
            IWICPalette* pal = NULL;
            if (SUCCEEDED(factory->CreatePalette(&pal)) && pal) {
                if (SUCCEEDED(pal->InitializeFromBitmap(bitmap, 256, TRUE)))
                    frame->SetPalette(pal);
                pal->Release();
            }
        }
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
                rc = sc_wic_write(sc_wide(path_utf8).c_str(), buf.data(), w, h,
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

struct ScCom {
    bool did = false;
    ScCom() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        did = (hr == S_OK || hr == S_FALSE);
    }
    ~ScCom() { if (did) CoUninitialize(); }
};

struct ScImage {
    std::vector<uint8_t> px;  // top-down 32bpp BGRA
    int w = 0, h = 0;
};

// Decodes the first frame of `src` (any WIC format) to BGRA, turned the way
// a JPEG's EXIF orientation says it is displayed. The file is closed again
// before this returns, so a target may name the same file.
static int sc_load(const char* src_utf8, ScImage& img) {
    if (!src_utf8 || !*src_utf8) return SC_E_DECODE;
    ScCom com;
    IWICImagingFactory*    factory = NULL;
    IWICBitmapDecoder*     decoder = NULL;
    IWICBitmapFrameDecode* frame   = NULL;
    IWICFormatConverter*   conv    = NULL;
    std::vector<uint8_t> buf;
    UINT w = 0, h = 0;
    int rc = SC_E_DECODE;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) goto done;
    hr = factory->CreateDecoderFromFilename(sc_wide(src_utf8).c_str(), NULL,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) goto done;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) goto done;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) goto done;
    if ((unsigned long long)w * h > SC_MAX_PIXELS) { rc = SC_E_SIZE; goto done; }
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) goto done;
    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto done;
    try {
        buf.resize((size_t)w * 4u * (size_t)h);
    } catch (const std::bad_alloc&) {
        rc = SC_E_SIZE;
        goto done;
    }
    hr = conv->CopyPixels(NULL, w * 4u, (UINT)buf.size(), buf.data());
    if (FAILED(hr)) goto done;
    rc = SC_OK;
done:
    if (conv)    conv->Release();
    if (frame)   frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (rc != SC_OK) return rc;

    int orientation = 1;
    jdb_image::Bytes file;
    if (jdb_image::read_file(src_utf8, file)) orientation = jdb_image::jpeg_orientation(file);
    if (orientation <= 1) {
        img.px.swap(buf);
        img.w = (int)w;
        img.h = (int)h;
        return SC_OK;
    }
    try {
        img.px.resize(buf.size());
    } catch (const std::bad_alloc&) {
        return SC_E_SIZE;
    }
    jdb_image::orient_pixels((const uint32_t*)buf.data(), (int)w, (int)h, orientation,
                             (uint32_t*)img.px.data(), img.w, img.h);
    return SC_OK;
}

static int sc_save(const char* dst_utf8, const ScImage& img) {
    return sc_wic_write(sc_wide(dst_utf8).c_str(), img.px.data(), img.w, img.h,
                        sc_container_for_ext(dst_utf8));
}

// Writes `src` to `dst` by its extension, as displayed.
extern "C" int jdb_imgconvert(const char* src_utf8, const char* dst_utf8) {
    if (!src_utf8 || !*src_utf8) return SC_E_DECODE;
    if (!dst_utf8 || !*dst_utf8) return SC_E_PATH;
    ScImage img;
    int rc = sc_load(src_utf8, img);
    return rc != SC_OK ? rc : sc_save(dst_utf8, img);
}

// The picture `src` on the clipboard as a DIB with alpha (CF_DIBV5, from
// which Windows makes CF_DIB and CF_BITMAP) and as PNG, which programs that
// keep transparency take first. false when it cannot be read or placed.
extern "C" int jdb_clip_setimage(const char* src_utf8) {
    ScImage img;
    if (sc_load(src_utf8, img) != SC_OK) return 0;
    size_t row = (size_t)img.w * 4;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + row * img.h);
    if (!dib) return 0;
    uint8_t* p = (uint8_t*)GlobalLock(dib);
    BITMAPV5HEADER hdr = {};
    hdr.bV5Size = sizeof(hdr);
    hdr.bV5Width = img.w;
    hdr.bV5Height = img.h;          // bottom-up, which every reader takes
    hdr.bV5Planes = 1;
    hdr.bV5BitCount = 32;
    hdr.bV5Compression = BI_BITFIELDS;
    hdr.bV5SizeImage = (DWORD)(row * img.h);
    hdr.bV5RedMask = 0x00FF0000;
    hdr.bV5GreenMask = 0x0000FF00;
    hdr.bV5BlueMask = 0x000000FF;
    hdr.bV5AlphaMask = 0xFF000000;
    hdr.bV5CSType = LCS_sRGB;
    hdr.bV5Intent = LCS_GM_IMAGES;
    memcpy(p, &hdr, sizeof(hdr));
    for (int y = 0; y < img.h; y++)
        memcpy(p + sizeof(hdr) + row * (img.h - 1 - y), &img.px[row * y], row);
    GlobalUnlock(dib);

    HGLOBAL png = NULL;
    {
        ScCom com;
        IWICImagingFactory* factory = NULL;
        IStream* stream = NULL;
        IWICBitmapEncoder* encoder = NULL;
        IWICBitmapFrameEncode* frame = NULL;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(CreateStreamOnHGlobal(NULL, FALSE, &stream)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, NULL)) &&
            SUCCEEDED(frame->Initialize(NULL)) &&
            SUCCEEDED(frame->SetSize((UINT)img.w, (UINT)img.h))) {
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            ok = SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
                 IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
                 SUCCEEDED(frame->WritePixels((UINT)img.h, (UINT)row, (UINT)img.px.size(),
                                              (BYTE*)img.px.data())) &&
                 SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()) &&
                 SUCCEEDED(GetHGlobalFromStream(stream, &png));
        }
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        if (!ok && png) { GlobalFree(png); png = NULL; }
    }

    if (!OpenClipboard(NULL)) {
        GlobalFree(dib);
        if (png) GlobalFree(png);
        return 0;
    }
    EmptyClipboard();
    bool placed = SetClipboardData(CF_DIBV5, dib) != NULL;
    if (!placed) GlobalFree(dib);
    if (png && !SetClipboardData(RegisterClipboardFormatW(L"PNG"), png)) GlobalFree(png);
    CloseClipboard();
    return placed ? 1 : 0;
}

// The picture on the clipboard (a 24- or 32-bit DIB) written to `dst` in the
// format of its extension; false when there is none.
extern "C" int jdb_clip_getimage(const char* dst_utf8) {
    if (!dst_utf8 || !*dst_utf8) return 0;
    if (!OpenClipboard(NULL)) return 0;
    HANDLE h = GetClipboardData(CF_DIBV5);
    if (!h) h = GetClipboardData(CF_DIB);
    ScImage img;
    bool ok = false;
    if (h) {
        SIZE_T total = GlobalSize(h);
        const uint8_t* p = (const uint8_t*)GlobalLock(h);
        if (p && total >= sizeof(BITMAPINFOHEADER)) {
            const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)p;
            int w = bi->biWidth;
            int hh = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
            bool bottom_up = bi->biHeight > 0;
            int bits = bi->biBitCount;
            size_t off = bi->biSize;
            if (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER)) off += 12;
            size_t row = ((size_t)w * bits / 8 + 3) & ~(size_t)3;
            bool plain = bi->biCompression == BI_RGB || bi->biCompression == BI_BITFIELDS;
            if (plain && (bits == 24 || bits == 32) && w > 0 && hh > 0 &&
                (unsigned long long)w * hh <= SC_MAX_PIXELS && off + row * hh <= total &&
                [&] {
                    // The clipboard is open and locked here: a failed
                    // allocation answers FALSE instead of leaving it so.
                    try { img.px.resize((size_t)w * hh * 4); } catch (const std::bad_alloc&) { return false; }
                    return true;
                }()) {
                img.w = w;
                img.h = hh;
                bool any_alpha = false;
                for (int y = 0; y < hh; y++) {
                    const uint8_t* src = p + off + row * (bottom_up ? hh - 1 - y : y);
                    uint8_t* dst = &img.px[(size_t)y * w * 4];
                    for (int x = 0; x < w; x++) {
                        dst[x * 4 + 0] = src[x * (bits / 8) + 0];
                        dst[x * 4 + 1] = src[x * (bits / 8) + 1];
                        dst[x * 4 + 2] = src[x * (bits / 8) + 2];
                        dst[x * 4 + 3] = bits == 32 ? src[x * 4 + 3] : 255;
                        if (bits == 32 && src[x * 4 + 3]) any_alpha = true;
                    }
                }
                // A 32-bit DIB that leaves alpha at 0 everywhere means opaque.
                if (bits == 32 && !any_alpha)
                    for (size_t i = 3; i < img.px.size(); i += 4) img.px[i] = 255;
                ok = true;
            }
        }
        if (p) GlobalUnlock(h);
    }
    CloseClipboard();
    return ok && sc_save(dst_utf8, img) == SC_OK ? 1 : 0;
}

// Writes `src` as displayed, turned clockwise by `quarters` quarter turns.
extern "C" int jdb_imgrotate(const char* src_utf8, const char* dst_utf8, int quarters) {
    if (!src_utf8 || !*src_utf8) return SC_E_DECODE;
    if (!dst_utf8 || !*dst_utf8) return SC_E_PATH;
    ScImage img;
    int rc = sc_load(src_utf8, img);
    if (rc != SC_OK) return rc;
    static const int turn_code[4] = { 1, 6, 3, 8 };
    int q = ((quarters % 4) + 4) % 4;
    if (q == 0) return sc_save(dst_utf8, img);
    ScImage out;
    try {
        out.px.resize(img.px.size());
    } catch (const std::bad_alloc&) {
        return SC_E_SIZE;
    }
    jdb_image::orient_pixels((const uint32_t*)img.px.data(), img.w, img.h, turn_code[q],
                             (uint32_t*)out.px.data(), out.w, out.h);
    return sc_save(dst_utf8, out);
}

// Writes the rectangle (x, y, w, h) of `src` as displayed to `dst`.
extern "C" int jdb_imgcrop(const char* src_utf8, const char* dst_utf8, int x, int y, int w, int h) {
    if (!src_utf8 || !*src_utf8) return SC_E_DECODE;
    if (!dst_utf8 || !*dst_utf8) return SC_E_PATH;
    ScImage img;
    int rc = sc_load(src_utf8, img);
    if (rc != SC_OK) return rc;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x > img.w - w || y > img.h - h) return SC_E_RECT;
    ScImage out;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * 4u * (size_t)h);
    for (int row = 0; row < h; row++)
        memcpy(&out.px[(size_t)row * w * 4u], &img.px[((size_t)(y + row) * img.w + x) * 4u], (size_t)w * 4u);
    return sc_save(dst_utf8, out);
}

// Writes `src` as displayed, scaled to w by h, to `dst`; a 0 follows from
// the other side so that the proportions stay.
extern "C" int jdb_imgresize(const char* src_utf8, const char* dst_utf8, int w, int h) {
    if (!src_utf8 || !*src_utf8) return SC_E_DECODE;
    if (!dst_utf8 || !*dst_utf8) return SC_E_PATH;
    if (w < 0 || h < 0 || (w == 0 && h == 0)) return SC_E_RECT;
    ScImage img;
    int rc = sc_load(src_utf8, img);
    if (rc != SC_OK) return rc;
    double nw = w, nh = h;
    if (w == 0) nw = (double)img.w * h / img.h;
    if (h == 0) nh = (double)img.h * w / img.w;
    nw = nw < 1 ? 1 : nw + 0.5;
    nh = nh < 1 ? 1 : nh + 0.5;
    if (nw * nh > (double)SC_MAX_PIXELS) return SC_E_RECT;
    ScImage out;
    out.w = (int)nw;
    out.h = (int)nh;
    try {
        out.px.resize((size_t)out.w * 4u * (size_t)out.h);
    } catch (const std::bad_alloc&) {
        return SC_E_SIZE;
    }
    ScCom com;
    IWICImagingFactory* factory = NULL;
    IWICBitmap*         bitmap  = NULL;
    IWICBitmapScaler*   scaler  = NULL;
    rc = SC_E_ENCODE;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) goto done;
    hr = factory->CreateBitmapFromMemory((UINT)img.w, (UINT)img.h, GUID_WICPixelFormat32bppBGRA,
            (UINT)img.w * 4u, (UINT)img.px.size(), img.px.data(), &bitmap);
    if (FAILED(hr)) goto done;
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) goto done;
    // Fant averages every pixel a target pixel covers when shrinking;
    // growing is interpolated by the cubic.
    hr = scaler->Initialize(bitmap, (UINT)out.w, (UINT)out.h,
            (out.w < img.w || out.h < img.h) ? WICBitmapInterpolationModeFant
                                             : WICBitmapInterpolationModeCubic);
    if (FAILED(hr)) goto done;
    hr = scaler->CopyPixels(NULL, (UINT)out.w * 4u, (UINT)out.px.size(), out.px.data());
    if (FAILED(hr)) goto done;
    rc = SC_OK;
done:
    if (scaler)  scaler->Release();
    if (bitmap)  bitmap->Release();
    if (factory) factory->Release();
    return rc != SC_OK ? rc : sc_save(dst_utf8, out);
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

#if !defined(_WIN32)
extern "C" int jdb_imgconvert(const char*, const char*) {
    return SC_UNSUPPORTED;
}
extern "C" int jdb_imgcrop(const char*, const char*, int, int, int, int) {
    return SC_UNSUPPORTED;
}
extern "C" int jdb_clip_setimage(const char*) {
    return 0;
}
extern "C" int jdb_clip_getimage(const char*) {
    return 0;
}
extern "C" int jdb_imgrotate(const char*, const char*, int) {
    return SC_UNSUPPORTED;
}
extern "C" int jdb_imgresize(const char*, const char*, int, int) {
    return SC_UNSUPPORTED;
}
#endif
