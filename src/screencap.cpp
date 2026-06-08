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

#else  // non-Windows: capture not implemented

extern "C" int jdb_screencap(const char*, const char*, const char*) {
    return SC_UNSUPPORTED;
}

#endif
