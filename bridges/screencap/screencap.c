/*
 * screencap - minimal jdBasic FFI bridge for GDI screen / window capture.
 *
 * Exposes a flat C API (no jdBasic types) that jdBasic calls via DECLARE
 * FUNC. The full BMP encode happens here in C because the FFI RETURN
 * marshaller decodes buffers as NUL-terminated strings, which would
 * truncate raw 32-bit pixel data at the first zero byte. Doing the
 * GetDIBits + file write on the C side avoids that entirely.
 *
 * Build into a DLL and place next to jdBasic.exe (build.bat does this).
 *
 *   intptr_t screencap_window(intptr_t hwnd, const char* path)
 *       Capture the CLIENT area of hwnd into a 32-bit BMP at `path`.
 *       Returns 0 on success, negative on error.
 *
 *   intptr_t screencap_screen(const char* path)
 *       Capture the whole primary screen into a 32-bit BMP at `path`.
 *       Returns 0 on success, negative on error.
 *
 * Build (Windows):  build.bat
 */

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_API __declspec(dllexport)

/* Error codes (all negative). */
#define SC_OK            0
#define SC_E_NODC      (-1)
#define SC_E_NOMEMDC   (-2)
#define SC_E_NOBITMAP  (-3)
#define SC_E_BLT       (-4)
#define SC_E_DIBITS    (-5)
#define SC_E_FILE      (-6)
#define SC_E_SIZE      (-7)

/*
 * Core capture: BitBlt a rectangle of `srcDC` (origin srcX,srcY, size w x h)
 * into a memory bitmap, then GetDIBits it and write a 32-bit BI_RGB .bmp.
 */
static intptr_t capture_rect(HDC srcDC, int srcX, int srcY, int w, int h,
                             const char* path)
{
    HDC      memDC   = NULL;
    HBITMAP  bmp     = NULL;
    HBITMAP  oldBmp  = NULL;
    char*    pixels  = NULL;
    HANDLE   file    = INVALID_HANDLE_VALUE;
    intptr_t rc      = SC_OK;
    DWORD    rowBytes, imgBytes, written;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER bi;

    if (w <= 0 || h <= 0) return SC_E_SIZE;

    memDC = CreateCompatibleDC(srcDC);
    if (!memDC) { rc = SC_E_NOMEMDC; goto done; }

    bmp = CreateCompatibleBitmap(srcDC, w, h);
    if (!bmp) { rc = SC_E_NOBITMAP; goto done; }

    oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    if (!BitBlt(memDC, 0, 0, w, h, srcDC, srcX, srcY, SRCCOPY)) {
        rc = SC_E_BLT; goto done;
    }

    /* 32-bit rows are inherently DWORD-aligned. */
    rowBytes = (DWORD)w * 4u;
    imgBytes = rowBytes * (DWORD)h;

    ZeroMemory(&bi, sizeof(bi));
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = w;
    bi.biHeight      = h;            /* positive => bottom-up DIB */
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    pixels = (char*)GlobalAlloc(GMEM_FIXED, imgBytes);
    if (!pixels) { rc = SC_E_SIZE; goto done; }

    if (!GetDIBits(memDC, bmp, 0, (UINT)h, pixels,
                   (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        rc = SC_E_DIBITS; goto done;
    }

    ZeroMemory(&fh, sizeof(fh));
    fh.bfType    = 0x4D42;  /* 'BM' */
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + imgBytes;

    file = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { rc = SC_E_FILE; goto done; }

    if (!WriteFile(file, &fh, sizeof(fh), &written, NULL) ||
        !WriteFile(file, &bi, sizeof(bi), &written, NULL) ||
        !WriteFile(file, pixels, imgBytes, &written, NULL)) {
        rc = SC_E_FILE; goto done;
    }

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (pixels) GlobalFree(pixels);
    if (memDC) {
        if (oldBmp) SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }
    if (bmp) DeleteObject(bmp);
    return rc;
}

SC_API intptr_t screencap_window(intptr_t hwnd, const char* path)
{
    HWND  win = (HWND)hwnd;
    HDC   dc;
    RECT  rc;
    POINT origin = { 0, 0 };
    intptr_t result;

    if (!path) return SC_E_FILE;
    if (!win || !IsWindow(win)) return SC_E_NODC;

    if (!GetClientRect(win, &rc)) return SC_E_NODC;

    /* Capture from the screen DC at the window's client origin so the
     * result is correct even if the window is partially covered by our
     * own toolwindows during an automated run. */
    ClientToScreen(win, &origin);

    dc = GetDC(NULL);
    if (!dc) return SC_E_NODC;

    result = capture_rect(dc, origin.x, origin.y,
                          rc.right - rc.left, rc.bottom - rc.top, path);

    ReleaseDC(NULL, dc);
    return result;
}

SC_API intptr_t screencap_screen(const char* path)
{
    HDC dc;
    int w, h;
    intptr_t result;

    if (!path) return SC_E_FILE;

    dc = GetDC(NULL);
    if (!dc) return SC_E_NODC;

    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);

    result = capture_rect(dc, 0, 0, w, h, path);

    ReleaseDC(NULL, dc);
    return result;
}

#ifdef __cplusplus
}
#endif
