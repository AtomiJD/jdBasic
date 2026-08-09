#include "forms.h"
#include "vm.h"
#include "value.h"

#if defined(FORMS) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <richedit.h>
#include <wincodec.h>

#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "windowscodecs.lib")

// Main-script directory, defined in main.cpp; relative .jdform paths
// resolve against it like the graphics asset loaders do.
extern std::string g_base_dir;

namespace {

// ── Handle registry ─────────────────────────────────────────────
// Forms and controls share one integer handle space. A control's Win32
// dialog-item id equals its jdBasic handle, so WM_COMMAND can map back.

struct FormsItem {
    HWND        hwnd = nullptr;
    std::string name;           // uppercase, used to build event names
    std::string kind;           // FORM, MDIFRAME, MDICHILD, BUTTON, MENU, ...
    int         form = 0;       // owning form handle (0 for a top-level form)
    HWND        mdi_client = nullptr;   // MDIFRAME only
    HACCEL      haccel = nullptr;       // forms with menu accelerators
    HWND        buddy = nullptr;        // UPDOWN's edit companion
};

std::map<int, FormsItem>  g_items;
std::map<HWND, int>       g_byhwnd;
int                       g_next_handle = 1;
int                       g_open_forms  = 0;
HFONT                     g_font        = nullptr;
VM*                       g_vm          = nullptr;
HWND                      g_last_form   = nullptr;
int                       g_dpi         = 96;

// Script-facing coordinates are logical 96-DPI units; the system message
// font comes back scaled to the system DPI, so the layout has to scale
// with it or every control clips on a >100% display.
int px(int logical) { return MulDiv(logical, g_dpi, 96); }
int lg(int device)  { return MulDiv(device, 96, g_dpi); }

struct PendingEvent {
    std::string name;
    Value       info;
};
std::deque<PendingEvent> g_evq;

// ── String helpers ──────────────────────────────────────────────

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string upname(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return r;
}

std::string value_text(const Value& v) {
    switch (v.type) {
        case ValueType::STRING:  return v.as_string()->data;
        case ValueType::BOOLEAN: return v.to_bool() ? "TRUE" : "FALSE";
        case ValueType::NONE:    return "";
        default: {
            double d = v.to_double();
            if (d == (double)(int64_t)d) return std::to_string((int64_t)d);
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", d);
            return buf;
        }
    }
}

// Resolve a data-file path: as given first, then against the script dir.
std::string resolve_data_path(const std::string& path) {
    if (GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    if (!g_base_dir.empty()) {
        std::string alt = g_base_dir + "/" + path;
        if (GetFileAttributesW(widen(alt).c_str()) != INVALID_FILE_ATTRIBUTES) return alt;
    }
    return path;
}

std::string get_hwnd_text(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return std::string();
    std::wstring w(len + 1, 0);
    GetWindowTextW(h, &w[0], len + 1);
    w.resize(len);
    return narrow(w);
}

// ── Registry access ─────────────────────────────────────────────

FormsItem& item_of(int handle, const char* who) {
    auto it = g_items.find(handle);
    if (it == g_items.end())
        throw std::runtime_error(std::string(who) + ": unknown forms handle " + std::to_string(handle));
    return it->second;
}

bool is_form_kind(const std::string& kind) {
    return kind == "FORM" || kind == "MDIFRAME" || kind == "MDICHILD";
}

FormsItem& form_of(int handle, const char* who) {
    FormsItem& f = item_of(handle, who);
    if (!is_form_kind(f.kind))
        throw std::runtime_error(std::string(who) + ": handle " + std::to_string(handle) + " is not a form");
    return f;
}

// ── Event queue ─────────────────────────────────────────────────
// Events are queued from the WndProc and dispatched from FORM.RUN /
// FORM.DOEVENTS on the VM thread, after DispatchMessage returns. Only
// events somebody registered a handler for are queued, so an unpumped
// window cannot grow the queue without bound.

void queue_event(const std::string& ctl_name, const char* event, Value info) {
    if (!g_vm) return;
    std::string name = ctl_name + "_" + event;
    if (!g_vm->event_handlers.count(name)) return;
    g_evq.push_back({std::move(name), std::move(info)});
}

Value info_map(const FormsItem& it) {
    Value m = Value::make_object();
    m.as_object()->set("name", Value::make_string(it.name));
    m.as_object()->set("handle", Value::make_i64(g_byhwnd.count(it.hwnd) ? g_byhwnd[it.hwnd] : 0));
    return m;
}

// Defined further down with the control subclass they mostly serve; the form
// WndProc needs them too.
void queue_key(const FormsItem& it, const char* ev, int code);
void queue_mouse(const FormsItem& it, const char* ev, int button, LPARAM lp);

void drain_events() {
    while (!g_evq.empty()) {
        PendingEvent ev = std::move(g_evq.front());
        g_evq.pop_front();
        g_vm->event_raise(ev.name, {ev.info});
    }
}

// Tooltip text per toolbar-button handle (TTN_GETDISPINFO lookup).
std::map<int, std::string> g_tooltips;

// Per-control GDI resources, freed in the destroy cascade.
std::map<int, HBRUSH>  g_shape_brushes;   // SHAPE fill
std::map<int, int>     g_shape_colors;    // SHAPE fill as 0xRRGGBB
std::map<int, HBITMAP> g_pictures;        // PICTURE bitmaps

// Appearance a control carries beyond its text. Windows asks the *parent*
// for a child's colours (WM_CTLCOLOR*), which is why these live here and not
// in the control: the form's WndProc answers out of these maps.
std::map<int, int>     g_fore;        // FORECOLOR as 0xRRGGBB
std::map<int, int>     g_back;        // BACKCOLOR as 0xRRGGBB
std::map<int, HBRUSH>  g_back_brush;  // the brush WM_CTLCOLOR* hands back
std::map<int, HFONT>   g_fonts;       // per-control FONT
std::map<int, Value>   g_tags;        // TAG, whatever the program puts there
std::map<int, std::string> g_ctl_tips;  // TOOLTIP text
std::map<int, HWND>    g_tip_wnd;     // one tooltip window per form
std::map<int, HCURSOR> g_cursors;     // CURSOR

COLORREF rgb_to_colorref(int rgb) {
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

// A font from { "name", "size", "bold", "italic", "underline", "strike" };
// anything absent stays at the system UI font, so setting only "bold" works.
HFONT make_font(const Value& spec) {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    if (spec.type == ValueType::OBJECT) {
        auto* o = spec.as_object();
        if (Value* v = o->get("name")) {
            std::wstring w = widen(v->to_string());
            wcsncpy_s(lf.lfFaceName, LF_FACESIZE, w.c_str(), _TRUNCATE);
        }
        if (Value* v = o->get("size"))      lf.lfHeight = -MulDiv((int)v->to_int(), g_dpi, 72);
        if (Value* v = o->get("bold"))      lf.lfWeight = v->to_bool() ? FW_BOLD : FW_NORMAL;
        if (Value* v = o->get("italic"))    lf.lfItalic = v->to_bool() ? 1 : 0;
        if (Value* v = o->get("underline")) lf.lfUnderline = v->to_bool() ? 1 : 0;
        if (Value* v = o->get("strike"))    lf.lfStrikeOut = v->to_bool() ? 1 : 0;
    }
    return CreateFontIndirectW(&lf);
}

// Answers a WM_CTLCOLOR* out of the maps above. False means the control has
// no colours of its own and the caller should carry on as before.
bool ctl_color(HDC hdc, HWND ctl, LRESULT& out);

// Swap one style bit group on a live control and make it repaint.
void restyle(HWND h, DWORD clear, DWORD set) {
    LONG_PTR s = GetWindowLongPtrW(h, GWL_STYLE);
    s = (s & ~(LONG_PTR)clear) | (LONG_PTR)set;
    SetWindowLongPtrW(h, GWL_STYLE, s);
    SetWindowPos(h, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(h, nullptr, TRUE);
}

// One tooltip window per form, created the first time a control wants one.
HWND tip_window(int frm, HWND form_hwnd) {
    auto it = g_tip_wnd.find(frm);
    if (it != g_tip_wnd.end() && IsWindow(it->second)) return it->second;
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               form_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_tip_wnd[frm] = tip;
    return tip;
}

// Tree nodes live outside the FormsItem registry: ids share the handle
// space, the maps tie them to their HTREEITEM and owning tree control.
struct TreeNode { HTREEITEM hitem; int tree; };
std::map<int, TreeNode>   g_tree_nodes;
std::map<HTREEITEM, int>  g_nodes_by_item;

// Tab-page membership: tab handle -> page index -> control handles.
std::map<int, std::map<int, std::vector<int>>> g_tab_pages;

// Append one row to a report listview: an array fills the subitems,
// a scalar becomes column 0.
void lv_add_row(HWND lv, const Value& row) {
    int idx = (int)SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0);
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = idx;
    if (row.type == ValueType::ARRAY) {
        auto& cells = row.as_array()->elements;
        std::wstring c0 = widen(cells.empty() ? std::string() : value_text(cells[0]));
        item.pszText = (LPWSTR)c0.c_str();
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&item);
        for (size_t i = 1; i < cells.size(); i++) {
            std::wstring ci = widen(value_text(cells[i]));
            LVITEMW sub = {};
            sub.iSubItem = (int)i;
            sub.pszText = (LPWSTR)ci.c_str();
            SendMessageW(lv, LVM_SETITEMTEXTW, idx, (LPARAM)&sub);
        }
    } else {
        std::wstring c0 = widen(value_text(row));
        item.pszText = (LPWSTR)c0.c_str();
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&item);
    }
}

std::string lv_cell_text(HWND lv, int row, int col) {
    wchar_t buf[512] = L"";
    LVITEMW item = {};
    item.iSubItem = col;
    item.pszText = buf;
    item.cchTextMax = 511;
    SendMessageW(lv, LVM_GETITEMTEXTW, row, (LPARAM)&item);
    return narrow(buf);
}

std::string tv_item_text(HWND tv, HTREEITEM hitem) {
    wchar_t buf[512] = L"";
    TVITEMW item = {};
    item.mask = TVIF_TEXT;
    item.hItem = hitem;
    item.pszText = buf;
    item.cchTextMax = 511;
    SendMessageW(tv, TVM_GETITEMW, 0, (LPARAM)&item);
    return narrow(buf);
}

// Show the current page's controls, hide every other page's.
void apply_tab_visibility(int tab_handle) {
    auto pit = g_tab_pages.find(tab_handle);
    if (pit == g_tab_pages.end()) return;
    auto tit = g_items.find(tab_handle);
    if (tit == g_items.end()) return;
    int cur = (int)SendMessageW(tit->second.hwnd, TCM_GETCURSEL, 0, 0);
    for (auto& [page, ctls] : pit->second)
        for (int ch : ctls) {
            auto cit = g_items.find(ch);
            if (cit != g_items.end() && cit->second.hwnd) {
                ShowWindow(cit->second.hwnd, page == cur ? SW_SHOW : SW_HIDE);
                if (cit->second.buddy)
                    ShowWindow(cit->second.buddy, page == cur ? SW_SHOW : SW_HIDE);
            }
        }
}

double systime_to_epoch(const SYSTEMTIME& st) {
    struct tm t = {};
    t.tm_year = st.wYear - 1900;
    t.tm_mon = st.wMonth - 1;
    t.tm_mday = st.wDay;
    t.tm_hour = st.wHour;
    t.tm_min = st.wMinute;
    t.tm_sec = st.wSecond;
    t.tm_isdst = -1;
    return (double)mktime(&t);
}

SYSTEMTIME epoch_to_systime(double epoch) {
    time_t tt = (time_t)epoch;
    struct tm local = {};
    localtime_s(&local, &tt);
    SYSTEMTIME st = {};
    st.wYear = (WORD)(local.tm_year + 1900);
    st.wMonth = (WORD)(local.tm_mon + 1);
    st.wDay = (WORD)local.tm_mday;
    st.wHour = (WORD)local.tm_hour;
    st.wMinute = (WORD)local.tm_min;
    st.wSecond = (WORD)local.tm_sec;
    return st;
}

// Decode any WIC-supported image (PNG/JPG/BMP/GIF/ICO) into a 32-bit
// DIB scaled to the requested device-pixel size.
HBITMAP load_image_file(const std::wstring& path, int dw, int dh) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fac))))
        return nullptr;
    HBITMAP hbmp = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;
    do {
        if (FAILED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &dec))) break;
        if (FAILED(dec->GetFrame(0, &frame))) break;
        if (dw <= 0 || dh <= 0) {
            UINT iw = 0, ih = 0;
            frame->GetSize(&iw, &ih);
            dw = (int)iw; dh = (int)ih;
        }
        if (FAILED(fac->CreateBitmapScaler(&scaler))) break;
        if (FAILED(scaler->Initialize(frame, dw, dh, WICBitmapInterpolationModeFant))) break;
        if (FAILED(fac->CreateFormatConverter(&conv))) break;
        if (FAILED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom))) break;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = dw;
        bi.bmiHeader.biHeight = -dh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp || !bits) { if (hbmp) { DeleteObject(hbmp); hbmp = nullptr; } break; }
        if (FAILED(conv->CopyPixels(nullptr, dw * 4, dw * dh * 4, (BYTE*)bits))) {
            DeleteObject(hbmp);
            hbmp = nullptr;
        }
    } while (false);
    if (conv) conv->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    fac->Release();
    return hbmp;
}

HWND toolbar_of(int form_handle) {
    for (auto& [h, it] : g_items)
        if (it.kind == "TOOLBAR" && it.form == form_handle) return it.hwnd;
    return nullptr;
}

HWND statusbar_of(int form_handle) {
    for (auto& [h, it] : g_items)
        if (it.kind == "STATUSBAR" && it.form == form_handle) return it.hwnd;
    return nullptr;
}

// Bring a freshly shown form in front of the launching editor/terminal.
// SetForegroundWindow alone is refused when another process holds the
// foreground, so ride the topmost toggle to the top of the z-order first.
void bring_to_front(HWND hwnd) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd);
}

// Re-flow a form after a resize or a bar change: the toolbar sizes itself
// along the top, the status bar along the bottom, and an MDI frame's
// client area fills what is left between them.
void layout_form(int handle) {
    FormsItem& f = g_items[handle];
    HWND tb = toolbar_of(handle);
    HWND sb = statusbar_of(handle);
    if (tb) SendMessageW(tb, TB_AUTOSIZE, 0, 0);
    if (sb) SendMessageW(sb, WM_SIZE, 0, 0);
    if (f.kind == "MDIFRAME" && f.mdi_client) {
        int top = 0, bottom = 0;
        RECT r;
        if (tb) { GetWindowRect(tb, &r); top = r.bottom - r.top; }
        if (sb) { GetWindowRect(sb, &r); bottom = r.bottom - r.top; }
        RECT cr;
        GetClientRect(f.hwnd, &cr);
        MoveWindow(f.mdi_client, 0, top, cr.right, cr.bottom - top - bottom, TRUE);
    }
}

// ── Window class / WndProc ──────────────────────────────────────

LRESULT CALLBACK form_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            HWND ctl = (HWND)lParam;
            if (!ctl) {
                // menu item or accelerator: the command id is the handle
                auto mit = g_items.find((int)LOWORD(wParam));
                if (mit != g_items.end() && mit->second.kind == "MENU") {
                    queue_event(mit->second.name, "CLICK", info_map(mit->second));
                    return 0;
                }
                // not ours: a maximized MDI child's caption buttons arrive
                // here as system command ids - DefFrameProc must see them
                break;
            }
            auto hit = g_byhwnd.find(ctl);
            if (hit == g_byhwnd.end()) break;
            FormsItem& it = g_items[hit->second];
            int code = HIWORD(wParam);

            if (it.kind == "TOOLBAR") {
                auto bit = g_items.find((int)LOWORD(wParam));
                if (bit != g_items.end() && bit->second.kind == "TOOLBTN") {
                    Value m = info_map(bit->second);
                    if (SendMessageW(ctl, TB_ISBUTTONCHECKED, LOWORD(wParam), 0))
                        m.as_object()->set("checked", Value::make_bool(true));
                    queue_event(bit->second.name, "CLICK", m);
                }
                return 0;
            }

            if (it.kind == "BUTTON" && code == BN_CLICKED) {
                queue_event(it.name, "CLICK", info_map(it));
            } else if ((it.kind == "CHECKBOX" || it.kind == "RADIO") && code == BN_CLICKED) {
                Value m = info_map(it);
                m.as_object()->set("checked",
                    Value::make_bool(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED));
                queue_event(it.name, "CLICK", m);
            } else if ((it.kind == "TEXTBOX" || it.kind == "RICHTEXT") && code == EN_CHANGE) {
                Value m = info_map(it);
                m.as_object()->set("text", Value::make_string(get_hwnd_text(ctl)));
                queue_event(it.name, "CHANGE", m);
            } else if (it.kind == "LISTBOX" && (code == LBN_SELCHANGE || code == LBN_DBLCLK)) {
                int idx = (int)SendMessageW(ctl, LB_GETCURSEL, 0, 0);
                Value m = info_map(it);
                m.as_object()->set("index", Value::make_i64(idx));
                if (idx >= 0) {
                    int len = (int)SendMessageW(ctl, LB_GETTEXTLEN, idx, 0);
                    std::wstring w(len + 1, 0);
                    SendMessageW(ctl, LB_GETTEXT, idx, (LPARAM)&w[0]);
                    w.resize(len);
                    m.as_object()->set("text", Value::make_string(narrow(w)));
                }
                queue_event(it.name, code == LBN_DBLCLK ? "DBLCLICK" : "CLICK", m);
            } else if (it.kind == "UPDOWN" && code == EN_CHANGE) {
                Value m = info_map(it);
                m.as_object()->set("value",
                    Value::make_i64((int)SendMessageW(it.hwnd, UDM_GETPOS32, 0, 0)));
                queue_event(it.name, "CHANGE", m);
            } else if (it.kind == "COMBO" && code == CBN_SELCHANGE) {
                int idx = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
                Value m = info_map(it);
                m.as_object()->set("index", Value::make_i64(idx));
                if (idx >= 0) {
                    int len = (int)SendMessageW(ctl, CB_GETLBTEXTLEN, idx, 0);
                    std::wstring w(len + 1, 0);
                    SendMessageW(ctl, CB_GETLBTEXT, idx, (LPARAM)&w[0]);
                    w.resize(len);
                    m.as_object()->set("text", Value::make_string(narrow(w)));
                }
                queue_event(it.name, "CHANGE", m);
            }
            return 0;
        }

        case WM_TIMER: {
            auto it = g_items.find((int)wParam);
            if (it != g_items.end() && it->second.kind == "TIMER")
                queue_event(it->second.name, "TICK", info_map(it->second));
            return 0;
        }

        case WM_SIZE: {
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end()) {
                FormsItem& it = g_items[hit->second];
                Value m = info_map(it);
                m.as_object()->set("width", Value::make_i64(lg(LOWORD(lParam))));
                m.as_object()->set("height", Value::make_i64(lg(HIWORD(lParam))));
                queue_event(it.name, "RESIZE", m);
                if (it.kind == "MDIFRAME") {
                    // own layout (toolbar strip + client below it) - skip
                    // DefFrameProc, which would size the client full-height
                    layout_form(hit->second);
                    return 0;
                }
                if (toolbar_of(hit->second) || statusbar_of(hit->second))
                    layout_form(hit->second);
            }
            // fall through: DefMDIChildProc maintains the maximized state
            break;
        }

        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lParam;
            if (!nm) break;
            if (nm->code == TTN_GETDISPINFOW) {
                NMTTDISPINFOW* di = (NMTTDISPINFOW*)lParam;
                auto tit = g_tooltips.find((int)di->hdr.idFrom);
                if (tit != g_tooltips.end()) {
                    std::wstring w = widen(tit->second);
                    wcsncpy(di->szText, w.c_str(), 79);
                    di->szText[79] = 0;
                }
                return 0;
            }
            auto hit = g_byhwnd.find(nm->hwndFrom);
            if (hit == g_byhwnd.end()) break;
            FormsItem& it = g_items[hit->second];

            if (it.kind == "LISTVIEW") {
                if (nm->code == LVN_ITEMCHANGED) {
                    NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
                    if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) {
                        Value m = info_map(it);
                        m.as_object()->set("index", Value::make_i64(lv->iItem));
                        m.as_object()->set("text",
                            Value::make_string(lv_cell_text(nm->hwndFrom, lv->iItem, 0)));
                        queue_event(it.name, "CLICK", m);
                    }
                } else if (nm->code == NM_DBLCLK) {
                    NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lParam;
                    if (ia->iItem >= 0) {
                        Value m = info_map(it);
                        m.as_object()->set("index", Value::make_i64(ia->iItem));
                        m.as_object()->set("text",
                            Value::make_string(lv_cell_text(nm->hwndFrom, ia->iItem, 0)));
                        queue_event(it.name, "DBLCLICK", m);
                    }
                }
                return 0;
            }
            if (it.kind == "TREEVIEW") {
                if (nm->code == TVN_SELCHANGEDW) {
                    NMTREEVIEWW* tv = (NMTREEVIEWW*)lParam;
                    auto nit = g_nodes_by_item.find(tv->itemNew.hItem);
                    Value m = info_map(it);
                    m.as_object()->set("node",
                        Value::make_i64(nit != g_nodes_by_item.end() ? nit->second : 0));
                    m.as_object()->set("text",
                        Value::make_string(tv_item_text(nm->hwndFrom, tv->itemNew.hItem)));
                    queue_event(it.name, "CLICK", m);
                } else if (nm->code == NM_DBLCLK) {
                    HTREEITEM sel = (HTREEITEM)SendMessageW(nm->hwndFrom, TVM_GETNEXTITEM,
                                                            TVGN_CARET, 0);
                    if (sel) {
                        auto nit = g_nodes_by_item.find(sel);
                        Value m = info_map(it);
                        m.as_object()->set("node",
                            Value::make_i64(nit != g_nodes_by_item.end() ? nit->second : 0));
                        m.as_object()->set("text",
                            Value::make_string(tv_item_text(nm->hwndFrom, sel)));
                        queue_event(it.name, "DBLCLICK", m);
                    }
                }
                return 0;
            }
            if (it.kind == "TABS" && nm->code == TCN_SELCHANGE) {
                apply_tab_visibility(hit->second);
                Value m = info_map(it);
                m.as_object()->set("index",
                    Value::make_i64((int)SendMessageW(nm->hwndFrom, TCM_GETCURSEL, 0, 0)));
                queue_event(it.name, "CHANGE", m);
                return 0;
            }
            if (it.kind == "DATETIME" && nm->code == DTN_DATETIMECHANGE) {
                NMDATETIMECHANGE* dc = (NMDATETIMECHANGE*)lParam;
                Value m = info_map(it);
                if (dc->dwFlags == GDT_VALID)
                    m.as_object()->set("value",
                        Value::make_f64(systime_to_epoch(dc->st)));
                queue_event(it.name, "CHANGE", m);
                return 0;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            LRESULT own;
            if (ctl_color(hdc, (HWND)lParam, own)) return own;
            auto sit = g_byhwnd.find((HWND)lParam);
            if (sit != g_byhwnd.end() && g_items[sit->second].kind == "SHAPE") {
                auto bit = g_shape_brushes.find(sit->second);
                if (bit != g_shape_brushes.end()) return (LRESULT)bit->second;
            }
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_HSCROLL:
        case WM_VSCROLL: {
            HWND ctl = (HWND)lParam;
            if (!ctl) break;
            auto sit = g_byhwnd.find(ctl);
            if (sit != g_byhwnd.end() && g_items[sit->second].kind == "SLIDER") {
                FormsItem& it = g_items[sit->second];
                Value m = info_map(it);
                m.as_object()->set("value",
                    Value::make_i64((int)SendMessageW(ctl, TBM_GETPOS, 0, 0)));
                queue_event(it.name, "CHANGE", m);
            }
            return 0;
        }

        // Windows asks the parent for a child's colours, so this is where a
        // control's FORECOLOR and BACKCOLOR are answered. A themed push button
        // draws itself and ignores the answer - same as VB6, where a button
        // needed Style = Graphical before BackColor did anything.
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX: {
            LRESULT own;
            if (ctl_color((HDC)wParam, (HWND)lParam, own)) return own;
            break;
        }

        case WM_ERASEBKGND: {
            auto hit2 = g_byhwnd.find(hwnd);
            if (hit2 != g_byhwnd.end()) {
                auto bb = g_back_brush.find(hit2->second);
                if (bb != g_back_brush.end()) {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    FillRect((HDC)wParam, &rc, bb->second);
                    return 1;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEMOVE: {
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end()) {
                FormsItem& it = g_items[hit->second];
                int btn = (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 2
                        : (msg == WM_MOUSEMOVE) ? 0 : 1;
                const char* ev = (msg == WM_MOUSEMOVE) ? "MOUSEMOVE"
                    : (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) ? "MOUSEUP" : "MOUSEDOWN";
                queue_mouse(it, ev, btn, lParam);
            }
            break;
        }

        case WM_CLOSE: {
            // The one event that cannot be queued. A handler has to run while
            // the window is still there: it may want to read its own fields
            // before saving them, and it may want to refuse outright. Setting
            // e[0]{"cancel"} keeps the form open, which is what VB6 spelled
            // Form_QueryUnload(Cancel).
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end()) {
                FormsItem& f = g_items[hit->second];
                std::string handler = f.name + "_UNLOAD";
                if (g_vm && g_vm->event_handlers.count(handler)) {
                    Value info = info_map(f);
                    info.as_object()->set("cancel", Value::make_bool(false));
                    g_vm->event_raise(handler, { info });
                    Value* c = info.as_object()->get("cancel");
                    if (c && c->to_bool()) return 0;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end()) {
                int form_handle = hit->second;
                bool counts = g_items[form_handle].kind != "MDICHILD";
                // Transitive cascade: a dying MDI frame takes its children,
                // and each child takes its own controls and menu items.
                std::unordered_set<int> dying = { form_handle };
                bool grew = true;
                while (grew) {
                    grew = false;
                    for (auto& [h2, it2] : g_items)
                        if (!dying.count(h2) && dying.count(it2.form) && is_form_kind(it2.kind)) {
                            dying.insert(h2);
                            grew = true;
                        }
                }
                for (auto it = g_items.begin(); it != g_items.end();) {
                    if (dying.count(it->first) || dying.count(it->second.form)) {
                        if (is_form_kind(it->second.kind) && it->second.haccel)
                            DestroyAcceleratorTable(it->second.haccel);
                        g_tooltips.erase(it->first);
                        auto bru = g_shape_brushes.find(it->first);
                        if (bru != g_shape_brushes.end()) {
                            DeleteObject(bru->second);
                            g_shape_brushes.erase(bru);
                        }
                        auto pic = g_pictures.find(it->first);
                        if (pic != g_pictures.end()) {
                            DeleteObject(pic->second);
                            g_pictures.erase(pic);
                        }
                        auto bb2 = g_back_brush.find(it->first);
                        if (bb2 != g_back_brush.end()) {
                            DeleteObject(bb2->second);
                            g_back_brush.erase(bb2);
                        }
                        auto fnt = g_fonts.find(it->first);
                        if (fnt != g_fonts.end()) {
                            DeleteObject(fnt->second);
                            g_fonts.erase(fnt);
                        }
                        g_fore.erase(it->first);
                        g_back.erase(it->first);
                        g_tags.erase(it->first);
                        g_ctl_tips.erase(it->first);
                        g_cursors.erase(it->first);
                        auto tw = g_tip_wnd.find(it->first);
                        if (tw != g_tip_wnd.end()) {
                            if (IsWindow(tw->second)) DestroyWindow(tw->second);
                            g_tip_wnd.erase(tw);
                        }
                        if (it->second.buddy) g_byhwnd.erase(it->second.buddy);
                        if (it->second.hwnd && it->second.kind != "TOOLBTN")
                            g_byhwnd.erase(it->second.hwnd);
                        g_tab_pages.erase(it->first);
                        for (auto nit = g_tree_nodes.begin(); nit != g_tree_nodes.end();) {
                            if (nit->second.tree == it->first) {
                                g_nodes_by_item.erase(nit->second.hitem);
                                nit = g_tree_nodes.erase(nit);
                            } else {
                                ++nit;
                            }
                        }
                        it = g_items.erase(it);
                    } else {
                        ++it;
                    }
                }
                // No PostQuitMessage: FORM.RUN and FORM.DOEVENTS re-check
                // g_open_forms after every dispatch, and a queued WM_QUIT
                // would leak into whichever message loop runs next. MDI
                // children never count toward the open-form total.
                if (counts && --g_open_forms < 0) g_open_forms = 0;
            }
            return 0;
        }
    }
    {
        auto hit = g_byhwnd.find(hwnd);
        if (hit != g_byhwnd.end()) {
            FormsItem& it = g_items[hit->second];
            if (it.kind == "MDIFRAME")
                return DefFrameProcW(hwnd, it.mdi_client, msg, wParam, lParam);
            if (it.kind == "MDICHILD")
                return DefMDIChildProcW(hwnd, msg, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t* FORM_CLASS = L"JDBForm";

void ensure_class() {
    static bool done = false;
    if (done) return;

    // Compiled .exes carry no application manifest, so common-controls v6
    // (visual styles) would never activate. shell32 ships a v6 manifest
    // as resource 124 - activate it for this thread and leave it active.
    // Redundant but harmless when the host exe is already manifested.
    ACTCTXW ac = { sizeof(ac) };
    wchar_t sysdir[MAX_PATH];
    std::wstring shell;
    if (GetSystemDirectoryW(sysdir, MAX_PATH)) {
        shell = std::wstring(sysdir) + L"\\shell32.dll";
        ac.lpSource = shell.c_str();
        ac.lpResourceName = MAKEINTRESOURCEW(124);
        ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
        HANDLE actx = CreateActCtxW(&ac);
        if (actx != INVALID_HANDLE_VALUE) {
            ULONG_PTR cookie = 0;
            ActivateActCtx(actx, &cookie);
        }
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    if (screen) {
        g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
        if (g_dpi <= 0) g_dpi = 96;
        ReleaseDC(nullptr, screen);
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = form_wndproc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = FORM_CLASS;
    RegisterClassW(&wc);

    WNDCLASSW cc = wc;
    cc.lpszClassName = L"JDBMDIChild";
    RegisterClassW(&cc);

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    done = true;
}

// ── Creation helpers ────────────────────────────────────────────

// Defined in the loader section below; every creation path binds the
// <NAME>_<EVENT> handlers that exist in the program, VB6-style.
void bind_handlers(const std::string& ctl_name, const std::string& kind);

// ── Control subclass: focus, keyboard, mouse ────────────────────
// A Win32 control keeps these messages to itself - WM_SETFOCUS, WM_CHAR and
// the mouse buttons never reach the parent - so without a subclass none of
// this vocabulary can be offered at all. queue_event drops anything nobody
// has a handler for, so an unused control costs one map lookup per message.

void queue_mods(Value& m) {
    m.as_object()->set("shift", Value::make_bool((GetKeyState(VK_SHIFT)   & 0x8000) != 0));
    m.as_object()->set("ctrl",  Value::make_bool((GetKeyState(VK_CONTROL) & 0x8000) != 0));
    m.as_object()->set("alt",   Value::make_bool((GetKeyState(VK_MENU)    & 0x8000) != 0));
}

void queue_key(const FormsItem& it, const char* ev, int code) {
    Value m = info_map(it);
    m.as_object()->set("key", Value::make_i64(code));
    queue_mods(m);
    queue_event(it.name, ev, std::move(m));
}

void queue_mouse(const FormsItem& it, const char* ev, int button, LPARAM lp) {
    Value m = info_map(it);
    m.as_object()->set("button", Value::make_i64(button));
    // logical units, like every other coordinate the FORM API takes
    m.as_object()->set("x", Value::make_i64(lg((int)(short)LOWORD(lp))));
    m.as_object()->set("y", Value::make_i64(lg((int)(short)HIWORD(lp))));
    queue_mods(m);
    queue_event(it.name, ev, std::move(m));
}

LRESULT CALLBACK ctl_sub_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                              UINT_PTR, DWORD_PTR) {
    auto hit = g_byhwnd.find(hwnd);
    if (hit != g_byhwnd.end()) {
        FormsItem& it = g_items[hit->second];
        switch (msg) {
            case WM_SETFOCUS:    queue_event(it.name, "GOTFOCUS", info_map(it)); break;
            case WM_KILLFOCUS:   queue_event(it.name, "LOSTFOCUS", info_map(it)); break;
            case WM_KEYDOWN:     queue_key(it, "KEYDOWN", (int)wp); break;
            case WM_KEYUP:       queue_key(it, "KEYUP", (int)wp); break;
            case WM_CHAR:        queue_key(it, "KEYPRESS", (int)wp); break;
            case WM_LBUTTONDOWN: queue_mouse(it, "MOUSEDOWN", 1, lp); break;
            case WM_LBUTTONUP:   queue_mouse(it, "MOUSEUP", 1, lp); break;
            case WM_RBUTTONDOWN: queue_mouse(it, "MOUSEDOWN", 2, lp); break;
            case WM_RBUTTONUP:   queue_mouse(it, "MOUSEUP", 2, lp); break;
            case WM_MOUSEMOVE:   queue_mouse(it, "MOUSEMOVE", 0, lp); break;
            case WM_SETCURSOR: {
                auto cur = g_cursors.find(hit->second);
                if (cur != g_cursors.end()) {
                    SetCursor(cur->second);
                    return TRUE;
                }
                break;
            }
            default: break;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

bool ctl_color(HDC hdc, HWND ctl, LRESULT& out) {
    auto cit = g_byhwnd.find(ctl);
    if (cit == g_byhwnd.end()) return false;
    int ch = cit->second;
    auto f = g_fore.find(ch);
    auto b = g_back.find(ch);
    if (f == g_fore.end() && b == g_back.end()) return false;
    if (f != g_fore.end()) SetTextColor(hdc, rgb_to_colorref(f->second));
    if (b != g_back.end()) {
        SetBkColor(hdc, rgb_to_colorref(b->second));
        out = (LRESULT)g_back_brush[ch];
        return true;
    }
    // colour on the text only: let whatever is behind the control show through
    SetBkMode(hdc, TRANSPARENT);
    auto fb = g_back_brush.find(g_items[ch].form);
    out = (LRESULT)(fb != g_back_brush.end() ? fb->second : GetSysColorBrush(COLOR_BTNFACE));
    return true;
}

int store_item(HWND hwnd, const std::string& name, const std::string& kind, int form) {
    int h = g_next_handle++;
    g_items[h] = { hwnd, upname(name), kind, form };
    if (hwnd) g_byhwnd[hwnd] = h;
    return h;
}

int create_control(int frm, const std::string& name, const wchar_t* cls,
                   const std::string& text, DWORD style, DWORD exstyle,
                   int x, int y, int w, int h, const std::string& kind) {
    FormsItem& f = form_of(frm, ("FORM." + kind).c_str());
    int handle = g_next_handle;   // reserved below by store_item
    HWND ctl = CreateWindowExW(exstyle, cls, widen(text).c_str(),
                               WS_CHILD | WS_VISIBLE | style,
                               px(x), px(y), px(w), px(h), f.hwnd,
                               (HMENU)(INT_PTR)handle,
                               GetModuleHandleW(nullptr), nullptr);
    if (!ctl)
        throw std::runtime_error("FORM." + kind + ": CreateWindow failed for '" + name + "'");
    if (g_font) SendMessageW(ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
    int stored = store_item(ctl, name, kind, frm);
    SetWindowSubclass(ctl, ctl_sub_proc, 1, 0);
    bind_handlers(g_items[stored].name, kind);
    return stored;
}

int do_create_form(const std::string& title, int cw, int ch, const std::string& name,
                   bool mdi_frame = false) {
    ensure_class();
    int dcw = px(cw), dch = px(ch);
    RECT rc = { 0, 0, dcw, dch };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExW(0, FORM_CLASS, widen(title).c_str(),
                                WS_OVERLAPPEDWINDOW,
                                (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw std::runtime_error("FORM.CREATE: CreateWindow failed");

    // AdjustWindowRect assumes 96-DPI frame metrics, which undersizes the
    // client area on a scaled display - measure and correct exactly.
    RECT cr;
    GetClientRect(hwnd, &cr);
    int dw = dcw - (cr.right - cr.left), dh = dch - (cr.bottom - cr.top);
    if (dw != 0 || dh != 0) {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetWindowPos(hwnd, nullptr, 0, 0,
                     wr.right - wr.left + dw, wr.bottom - wr.top + dh,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    g_open_forms++;
    g_last_form = hwnd;
    int handle = store_item(hwnd, name, mdi_frame ? "MDIFRAME" : "FORM", 0);
    bind_handlers(g_items[handle].name, g_items[handle].kind);
    if (mdi_frame) {
        CLIENTCREATESTRUCT ccs = { nullptr, 50000 };
        g_items[handle].mdi_client = CreateWindowExW(
            0, L"MDICLIENT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | MDIS_ALLCHILDSTYLES,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)1,
            GetModuleHandleW(nullptr), &ccs);
        RECT cr2;
        GetClientRect(hwnd, &cr2);
        MoveWindow(g_items[handle].mdi_client, 0, 0, cr2.right, cr2.bottom, TRUE);
    }
    return handle;
}

int do_create_child(int frame, const std::string& title, int cw, int ch,
                    const std::string& name) {
    FormsItem& f = item_of(frame, "FORM.CHILD");
    if (f.kind != "MDIFRAME" || !f.mdi_client)
        throw std::runtime_error("FORM.CHILD: handle " + std::to_string(frame) +
                                 " is not an MDI frame (use FORM.MDI)");
    HWND hwnd = CreateWindowExW(WS_EX_MDICHILD, L"JDBMDIChild", widen(title).c_str(),
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, px(cw), px(ch),
                                f.mdi_client, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw std::runtime_error("FORM.CHILD: CreateWindow failed");
    int handle = store_item(hwnd, name, "MDICHILD", frame);
    bind_handlers(g_items[handle].name, "MDICHILD");
    queue_event(g_items[handle].name, "LOAD", info_map(g_items[handle]));
    return handle;
}

int arg_int(const std::vector<Value>& args, size_t i) { return (int)args[i].to_int(); }

std::string arg_str(const std::vector<Value>& args, size_t i, const char* who) {
    if (args[i].type != ValueType::STRING)
        throw std::runtime_error(std::string(who) + ": argument " + std::to_string(i + 1) + " must be a string");
    return args[i].as_string()->data;
}

// ── Message pumping ─────────────────────────────────────────────

// Pump one message, returns FALSE on WM_QUIT. Menu accelerators and the
// MDI system keys (Ctrl+F4/F6) go first; Tab/arrow navigation then runs
// through IsDialogMessage against the active MDI child or the top-level
// window the message belongs to.
bool pump_one(MSG& msg) {
    if (msg.message == WM_QUIT) return false;
    HWND root = GetAncestor(msg.hwnd, GA_ROOT);
    auto rit = root ? g_byhwnd.find(root) : g_byhwnd.end();
    if (rit != g_byhwnd.end()) {
        FormsItem& rf = g_items[rit->second];
        if (rf.kind == "MDIFRAME" && rf.mdi_client &&
            TranslateMDISysAccel(rf.mdi_client, &msg)) return true;
        if (rf.haccel && TranslateAcceleratorW(root, rf.haccel, &msg)) return true;
        HWND dlg = root;
        if (rf.kind == "MDIFRAME" && rf.mdi_client) {
            HWND active = (HWND)SendMessageW(rf.mdi_client, WM_MDIGETACTIVE, 0, 0);
            if (active) dlg = active;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return true;
    }
    if (!root || !IsDialogMessageW(root, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ── INPUTBOX$ modal dialog ──────────────────────────────────────

struct InputBoxState {
    HWND edit = nullptr;
    bool done = false;
    bool ok   = false;
};
InputBoxState* g_ibox = nullptr;

LRESULT CALLBACK inputbox_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && g_ibox) {
        int id = LOWORD(wParam);
        if (id == IDOK)     { g_ibox->ok = true;  g_ibox->done = true; return 0; }
        if (id == IDCANCEL) { g_ibox->ok = false; g_ibox->done = true; return 0; }
    }
    if (msg == WM_CLOSE && g_ibox) { g_ibox->ok = false; g_ibox->done = true; return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string run_inputbox(const std::string& prompt, const std::string& title,
                         const std::string& deflt) {
    ensure_class();
    static bool cls_done = false;
    if (!cls_done) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = inputbox_wndproc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"JDBInputBox";
        RegisterClassW(&wc);
        cls_done = true;
    }

    HWND owner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
    const int W = 380, H = 150;
    RECT rc = { 0, 0, px(W), px(H) };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"JDBInputBox", widen(title).c_str(),
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND lbl  = CreateWindowExW(0, L"STATIC", widen(prompt).c_str(),
                                WS_CHILD | WS_VISIBLE,
                                px(12), px(12), px(W - 24), px(40),
                                dlg, nullptr, inst, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(deflt).c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                px(12), px(60), px(W - 24), px(24),
                                dlg, nullptr, inst, nullptr);
    HWND okb  = CreateWindowExW(0, L"BUTTON", L"OK",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                px(W - 180), px(100), px(80), px(26),
                                dlg, (HMENU)IDOK, inst, nullptr);
    HWND cnb  = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                px(W - 92), px(100), px(80), px(26),
                                dlg, (HMENU)IDCANCEL, inst, nullptr);
    if (g_font)
        for (HWND h : { lbl, edit, okb, cnb })
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);

    InputBoxState st;
    st.edit = edit;
    g_ibox = &st;
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { st.ok = true;  st.done = true; continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { st.ok = false; st.done = true; continue; }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_ibox = nullptr;

    std::string result = st.ok ? get_hwnd_text(edit) : std::string();
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(dlg);
    return result;
}

// ── Property access ─────────────────────────────────────────────

void reset_items(HWND h, const std::string& kind, const Value& arr) {
    UINT reset = kind == "COMBO" ? CB_RESETCONTENT : LB_RESETCONTENT;
    UINT add   = kind == "COMBO" ? CB_ADDSTRING    : LB_ADDSTRING;
    SendMessageW(h, reset, 0, 0);
    if (arr.type != ValueType::ARRAY) return;
    for (auto& el : arr.as_array()->elements)
        SendMessageW(h, add, 0, (LPARAM)widen(value_text(el)).c_str());
}

void set_prop(int handle, FormsItem& it, const std::string& prop, const Value& v, const char* who) {
    if (it.kind == "MENU") {
        FormsItem& f = form_of(it.form, who);
        HMENU bar = GetMenu(f.hwnd);
        if (prop == "ENABLED") {
            EnableMenuItem(bar, handle, MF_BYCOMMAND | (v.to_bool() ? MF_ENABLED : MF_GRAYED));
        } else if (prop == "CHECKED") {
            CheckMenuItem(bar, handle, MF_BYCOMMAND | (v.to_bool() ? MF_CHECKED : MF_UNCHECKED));
        } else if (prop == "TEXT") {
            ModifyMenuW(bar, handle, MF_BYCOMMAND | MF_STRING, handle,
                        widen(value_text(v)).c_str());
            DrawMenuBar(f.hwnd);
        } else if (prop == "VALUE") {
            if (v.to_bool())
                SendMessageW(f.hwnd, WM_COMMAND, MAKEWPARAM(handle, 0), 0);
        } else {
            throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                     "' for MENU");
        }
        return;
    }

    if (it.kind == "TOOLBTN") {
        if (prop == "ENABLED") {
            SendMessageW(it.hwnd, TB_ENABLEBUTTON, handle, MAKELPARAM(v.to_bool() ? 1 : 0, 0));
        } else if (prop == "CHECKED") {
            SendMessageW(it.hwnd, TB_CHECKBUTTON, handle, MAKELPARAM(v.to_bool() ? 1 : 0, 0));
        } else if (prop == "VALUE") {
            if (v.to_bool())
                SendMessageW(GetParent(it.hwnd), WM_COMMAND, MAKEWPARAM(handle, 0),
                             (LPARAM)it.hwnd);
        } else {
            throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                     "' for TOOLBTN");
        }
        return;
    }

    if (it.kind == "STATUSBAR") {
        if (prop == "TEXT") {
            if (v.type == ValueType::ARRAY) {
                auto& els = v.as_array()->elements;
                for (size_t i = 0; i < els.size(); i++)
                    SendMessageW(it.hwnd, SB_SETTEXTW, i,
                                 (LPARAM)widen(value_text(els[i])).c_str());
            } else {
                SendMessageW(it.hwnd, SB_SETTEXTW, 0,
                             (LPARAM)widen(value_text(v)).c_str());
            }
            return;
        }
        if (prop == "VISIBLE") {
            ShowWindow(it.hwnd, v.to_bool() ? SW_SHOW : SW_HIDE);
            layout_form(it.form);
            return;
        }
        throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                 "' for STATUSBAR");
    }

    bool is_list = it.kind == "LISTBOX" || it.kind == "COMBO";
    HWND h = it.hwnd;

    if (prop == "MAXIMIZED" && is_form_kind(it.kind)) {
        if (it.kind == "MDICHILD") {
            FormsItem& frame = item_of(it.form, who);
            SendMessageW(frame.mdi_client, v.to_bool() ? WM_MDIMAXIMIZE : WM_MDIRESTORE,
                         (WPARAM)h, 0);
        } else {
            ShowWindow(h, v.to_bool() ? SW_MAXIMIZE : SW_RESTORE);
        }
        return;
    }

    if (prop == "TOPMOST" && is_form_kind(it.kind)) {
        SetWindowPos(h, v.to_bool() ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE);
        return;
    }

    if (prop == "TEXT") {
        SetWindowTextW(h, widen(value_text(v)).c_str());
    } else if (prop == "ENABLED") {
        EnableWindow(h, v.to_bool());
    } else if (prop == "VISIBLE") {
        ShowWindow(h, v.to_bool() ? SW_SHOW : SW_HIDE);
    } else if (prop == "CHECKED") {
        SendMessageW(h, BM_SETCHECK, v.to_bool() ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (prop == "ITEMS" && is_list) {
        reset_items(h, it.kind, v);
    } else if (prop == "ADDITEM" && is_list) {
        SendMessageW(h, it.kind == "COMBO" ? CB_ADDSTRING : LB_ADDSTRING,
                     0, (LPARAM)widen(value_text(v)).c_str());
    } else if (prop == "CLEAR" && is_list) {
        SendMessageW(h, it.kind == "COMBO" ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
    } else if (prop == "SELINDEX" && is_list) {
        SendMessageW(h, it.kind == "COMBO" ? CB_SETCURSEL : LB_SETCURSEL,
                     (WPARAM)v.to_int(), 0);
    } else if (prop == "VALUE" && it.kind == "BUTTON") {
        if (v.to_bool()) SendMessageW(h, BM_CLICK, 0, 0);
    } else if (prop == "VALUE" && it.kind == "PROGRESS") {
        SendMessageW(h, PBM_SETPOS, (WPARAM)v.to_int(), 0);
    } else if (prop == "VALUE" && it.kind == "SLIDER") {
        SendMessageW(h, TBM_SETPOS, TRUE, (LPARAM)v.to_int());
    } else if (prop == "VALUE" && it.kind == "UPDOWN") {
        SendMessageW(h, UDM_SETPOS32, 0, (LPARAM)v.to_int());
    } else if (prop == "MAX" && it.kind == "PROGRESS") {
        SendMessageW(h, PBM_SETRANGE32, 0, (LPARAM)v.to_int());
    } else if (prop == "MAX" && it.kind == "SLIDER") {
        SendMessageW(h, TBM_SETRANGEMAX, TRUE, (LPARAM)v.to_int());
    } else if (prop == "MIN" && it.kind == "SLIDER") {
        SendMessageW(h, TBM_SETRANGEMIN, TRUE, (LPARAM)v.to_int());
    } else if ((prop == "MIN" || prop == "MAX") && it.kind == "UPDOWN") {
        int lo = 0, hi = 0;
        SendMessageW(h, UDM_GETRANGE32, (WPARAM)&lo, (LPARAM)&hi);
        if (prop == "MIN") lo = (int)v.to_int(); else hi = (int)v.to_int();
        SendMessageW(h, UDM_SETRANGE32, lo, hi);
    } else if (prop == "FORECOLOR") {
        g_fore[handle] = (int)v.to_int();
        InvalidateRect(h, nullptr, TRUE);
    } else if (prop == "BACKCOLOR") {
        auto old = g_back_brush.find(handle);
        if (old != g_back_brush.end()) DeleteObject(old->second);
        int rgb = (int)v.to_int();
        g_back[handle] = rgb;
        g_back_brush[handle] = CreateSolidBrush(rgb_to_colorref(rgb));
        InvalidateRect(h, nullptr, TRUE);
    } else if (prop == "FONT") {
        auto old = g_fonts.find(handle);
        if (old != g_fonts.end()) DeleteObject(old->second);
        HFONT hf = make_font(v);
        g_fonts[handle] = hf;
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
        if (it.buddy) SendMessageW(it.buddy, WM_SETFONT, (WPARAM)hf, TRUE);
        InvalidateRect(h, nullptr, TRUE);
    } else if (prop == "TAG") {
        g_tags[handle] = v;
    } else if (prop == "TOOLTIP") {
        std::string text = v.to_string();
        g_ctl_tips[handle] = text;
        HWND fh = g_items.count(it.form) ? g_items[it.form].hwnd : h;
        HWND tip = tip_window(it.form ? it.form : handle, fh);
        if (tip) {
            std::wstring w = widen(text);
            TTTOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = fh;
            ti.uId = (UINT_PTR)h;
            ti.lpszText = (LPWSTR)w.c_str();
            SendMessageW(tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
            SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    } else if (prop == "ALIGN") {
        std::string a = upname(v.to_string());
        bool edit = (it.kind == "TEXTBOX" || it.kind == "RICHTEXT");
        DWORD all = edit ? (ES_LEFT | ES_CENTER | ES_RIGHT) : (SS_LEFT | SS_CENTER | SS_RIGHT);
        DWORD want = edit ? ES_LEFT : SS_LEFT;
        if (a == "CENTER") want = edit ? ES_CENTER : SS_CENTER;
        else if (a == "RIGHT") want = edit ? ES_RIGHT : SS_RIGHT;
        restyle(h, all, want);
    } else if (prop == "MAXLENGTH") {
        SendMessageW(h, EM_SETLIMITTEXT, (WPARAM)v.to_int(), 0);
    } else if (prop == "PASSWORD") {
        // an empty string or FALSE turns it back into a plain field
        std::string s = (v.type == ValueType::STRING) ? v.as_string()->data : "";
        wchar_t ch = 0;
        if (!s.empty()) ch = widen(s)[0];
        else if (v.type != ValueType::STRING && v.to_bool()) ch = L'*';
        SendMessageW(h, EM_SETPASSWORDCHAR, (WPARAM)ch, 0);
        InvalidateRect(h, nullptr, TRUE);
    } else if (prop == "LOCKED") {
        SendMessageW(h, EM_SETREADONLY, (WPARAM)(v.to_bool() ? TRUE : FALSE), 0);
    } else if (prop == "TABSTOP") {
        restyle(h, WS_TABSTOP, v.to_bool() ? WS_TABSTOP : 0);
    } else if (prop == "TABINDEX") {
        // Tab order in Win32 is the sibling z-order. Position 0 means first.
        int want = (int)v.to_int();
        std::vector<HWND> sibs;
        for (HWND s = GetWindow(GetParent(h), GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
            if (s != h) sibs.push_back(s);
        if (want <= 0) {
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            size_t after = (size_t)want - 1;
            if (after >= sibs.size()) after = sibs.empty() ? 0 : sibs.size() - 1;
            if (!sibs.empty())
                SetWindowPos(h, sibs[after], 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } else if (prop == "CURSOR") {
        std::string c = upname(v.to_string());
        LPCWSTR id = (LPCWSTR)IDC_ARROW;
        if (c == "WAIT" || c == "HOURGLASS") id = (LPCWSTR)IDC_WAIT;
        else if (c == "HAND") id = (LPCWSTR)IDC_HAND;
        else if (c == "IBEAM" || c == "TEXT") id = (LPCWSTR)IDC_IBEAM;
        else if (c == "CROSS") id = (LPCWSTR)IDC_CROSS;
        else if (c == "SIZEALL") id = (LPCWSTR)IDC_SIZEALL;
        else if (c == "NO") id = (LPCWSTR)IDC_NO;
        g_cursors[handle] = LoadCursorW(nullptr, id);
    } else if (prop == "COLOR" && it.kind == "SHAPE") {
        int rgb = (int)v.to_int();
        auto old = g_shape_brushes.find(handle);
        if (old != g_shape_brushes.end()) DeleteObject(old->second);
        g_shape_colors[handle] = rgb;
        g_shape_brushes[handle] = CreateSolidBrush(rgb_to_colorref(rgb));
        InvalidateRect(h, nullptr, TRUE);
    } else if (prop == "PICTURE" && it.kind == "PICTURE") {
        RECT rc;
        GetClientRect(h, &rc);
        std::string path = resolve_data_path(value_text(v));
        HBITMAP bmp = load_image_file(widen(path), rc.right, rc.bottom);
        if (!bmp)
            throw std::runtime_error(std::string(who) + ": cannot load image '" + path + "'");
        auto old = g_pictures.find(handle);
        if (old != g_pictures.end()) DeleteObject(old->second);
        g_pictures[handle] = bmp;
        SendMessageW(h, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp);
    } else if (prop == "TEXT" && it.kind == "UPDOWN") {
        SetWindowTextW(it.buddy, widen(value_text(v)).c_str());
    } else if (prop == "COLUMNS" && it.kind == "LISTVIEW") {
        while (SendMessageW(h, LVM_DELETECOLUMN, 0, 0)) {}
        if (v.type == ValueType::ARRAY) {
            auto& cols = v.as_array()->elements;
            for (size_t i = 0; i < cols.size(); i++) {
                std::string text;
                int width = 100;
                if (cols[i].type == ValueType::OBJECT) {
                    const Value* tv2 = cols[i].as_object()->get("text");
                    const Value* wv = cols[i].as_object()->get("width");
                    if (tv2) text = value_text(*tv2);
                    if (wv) width = (int)wv->to_int();
                } else {
                    text = value_text(cols[i]);
                }
                std::wstring wt = widen(text);
                LVCOLUMNW col = {};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = (LPWSTR)wt.c_str();
                col.cx = px(width);
                SendMessageW(h, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
            }
        }
    } else if (prop == "ITEMS" && it.kind == "LISTVIEW") {
        SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
        if (v.type == ValueType::ARRAY)
            for (auto& row : v.as_array()->elements) lv_add_row(h, row);
    } else if (prop == "ADDITEM" && it.kind == "LISTVIEW") {
        lv_add_row(h, v);
    } else if (prop == "CLEAR" && it.kind == "LISTVIEW") {
        SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
    } else if (prop == "SELINDEX" && it.kind == "LISTVIEW") {
        LVITEMW item = {};
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        item.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)v.to_int(), (LPARAM)&item);
    } else if (prop == "CLEAR" && it.kind == "TREEVIEW") {
        SendMessageW(h, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
        for (auto nit = g_tree_nodes.begin(); nit != g_tree_nodes.end();) {
            if (nit->second.tree == handle) {
                g_nodes_by_item.erase(nit->second.hitem);
                nit = g_tree_nodes.erase(nit);
            } else {
                ++nit;
            }
        }
    } else if (prop == "SELNODE" && it.kind == "TREEVIEW") {
        auto nit = g_tree_nodes.find((int)v.to_int());
        if (nit != g_tree_nodes.end())
            SendMessageW(h, TVM_SELECTITEM, TVGN_CARET, (LPARAM)nit->second.hitem);
    } else if (prop == "EXPAND" && it.kind == "TREEVIEW") {
        auto nit = g_tree_nodes.find((int)v.to_int());
        if (nit != g_tree_nodes.end())
            SendMessageW(h, TVM_EXPAND, TVE_EXPAND, (LPARAM)nit->second.hitem);
    } else if (prop == "SELINDEX" && it.kind == "TABS") {
        SendMessageW(h, TCM_SETCURSEL, (WPARAM)v.to_int(), 0);
        apply_tab_visibility(handle);
    } else if (prop == "VALUE" && it.kind == "DATETIME") {
        SYSTEMTIME st = epoch_to_systime(v.to_double());
        SendMessageW(h, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
    } else if (prop == "FOCUS") {
        SetFocus(h);
    } else if (prop == "INTERVAL" && it.kind == "TIMER") {
        FormsItem& f = form_of(it.form, who);
        int ms = (int)v.to_int();
        if (ms > 0) SetTimer(f.hwnd, handle, ms, nullptr);
        else        KillTimer(f.hwnd, handle);
    } else if (prop == "X" || prop == "Y" || prop == "WIDTH" || prop == "HEIGHT") {
        RECT rc;
        GetWindowRect(h, &rc);
        POINT tl = { rc.left, rc.top };
        HWND parent = GetParent(h);
        if (parent) ScreenToClient(parent, &tl);
        int x = tl.x, y = tl.y, w = rc.right - rc.left, hh = rc.bottom - rc.top;
        int n = px((int)v.to_int());
        if (prop == "X") x = n; else if (prop == "Y") y = n;
        else if (prop == "WIDTH") w = n; else hh = n;
        MoveWindow(h, x, y, w, hh, TRUE);
    } else {
        throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                 "' for " + it.kind);
    }
}

Value get_prop(int handle, FormsItem& it, const std::string& prop, const char* who) {
    if (it.kind == "MENU") {
        if (prop == "NAME") return Value::make_string(it.name);
        if (prop == "KIND") return Value::make_string(it.kind);
        FormsItem& f = form_of(it.form, who);
        UINT state = GetMenuState(GetMenu(f.hwnd), handle, MF_BYCOMMAND);
        if (prop == "CHECKED") return Value::make_bool((state & MF_CHECKED) != 0);
        if (prop == "ENABLED") return Value::make_bool((state & (MF_GRAYED | MF_DISABLED)) == 0);
        throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                 "' for MENU");
    }

    if (it.kind == "TOOLBTN") {
        if (prop == "NAME") return Value::make_string(it.name);
        if (prop == "KIND") return Value::make_string(it.kind);
        if (prop == "ENABLED")
            return Value::make_bool(SendMessageW(it.hwnd, TB_ISBUTTONENABLED, handle, 0) != 0);
        if (prop == "CHECKED")
            return Value::make_bool(SendMessageW(it.hwnd, TB_ISBUTTONCHECKED, handle, 0) != 0);
        throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                 "' for TOOLBTN");
    }

    if (it.kind == "STATUSBAR" && prop == "TEXT") {
        int len = (int)LOWORD(SendMessageW(it.hwnd, SB_GETTEXTLENGTHW, 0, 0));
        std::wstring w(len + 1, 0);
        SendMessageW(it.hwnd, SB_GETTEXTW, 0, (LPARAM)&w[0]);
        w.resize(len);
        return Value::make_string(narrow(w));
    }

    bool is_list = it.kind == "LISTBOX" || it.kind == "COMBO";
    HWND h = it.hwnd;

    if (prop == "MAXIMIZED" && is_form_kind(it.kind))
        return Value::make_bool(IsZoomed(h) != 0);
    if (prop == "TOPMOST" && is_form_kind(it.kind))
        return Value::make_bool((GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    if (prop == "VALUE" && it.kind == "PROGRESS")
        return Value::make_i64((int)SendMessageW(h, PBM_GETPOS, 0, 0));
    if (prop == "VALUE" && it.kind == "SLIDER")
        return Value::make_i64((int)SendMessageW(h, TBM_GETPOS, 0, 0));
    if (prop == "VALUE" && it.kind == "UPDOWN")
        return Value::make_i64((int)SendMessageW(h, UDM_GETPOS32, 0, 0));
    if (prop == "FORECOLOR") {
        auto f = g_fore.find(handle);
        return Value::make_i64(f == g_fore.end() ? -1 : f->second);
    }
    if (prop == "BACKCOLOR") {
        auto b = g_back.find(handle);
        return Value::make_i64(b == g_back.end() ? -1 : b->second);
    }
    if (prop == "TAG") {
        auto t = g_tags.find(handle);
        return t == g_tags.end() ? Value::make_none() : t->second;
    }
    if (prop == "TOOLTIP") {
        auto t = g_ctl_tips.find(handle);
        return Value::make_string(t == g_ctl_tips.end() ? "" : t->second);
    }
    if (prop == "MAXLENGTH")
        return Value::make_i64((int64_t)SendMessageW(h, EM_GETLIMITTEXT, 0, 0));
    if (prop == "LOCKED")
        return Value::make_bool((GetWindowLongPtrW(h, GWL_STYLE) & ES_READONLY) != 0);
    if (prop == "TABSTOP")
        return Value::make_bool((GetWindowLongPtrW(h, GWL_STYLE) & WS_TABSTOP) != 0);
    if (prop == "COLOR" && it.kind == "SHAPE") {
        auto cit = g_shape_colors.find(handle);
        return Value::make_i64(cit != g_shape_colors.end() ? cit->second : 0);
    }
    if (prop == "TEXT" && it.kind == "UPDOWN")
        return Value::make_string(get_hwnd_text(it.buddy));
    if (it.kind == "LISTVIEW") {
        if (prop == "COUNT")
            return Value::make_i64((int)SendMessageW(h, LVM_GETITEMCOUNT, 0, 0));
        if (prop == "SELINDEX")
            return Value::make_i64((int)SendMessageW(h, LVM_GETNEXTITEM, (WPARAM)-1,
                                                     LVNI_SELECTED));
        if (prop == "SELTEXT") {
            int sel = (int)SendMessageW(h, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
            return Value::make_string(sel >= 0 ? lv_cell_text(h, sel, 0) : "");
        }
    }
    if (it.kind == "TREEVIEW") {
        if (prop == "COUNT")
            return Value::make_i64((int)SendMessageW(h, TVM_GETCOUNT, 0, 0));
        if (prop == "SELNODE") {
            HTREEITEM sel = (HTREEITEM)SendMessageW(h, TVM_GETNEXTITEM, TVGN_CARET, 0);
            auto nit = sel ? g_nodes_by_item.find(sel) : g_nodes_by_item.end();
            return Value::make_i64(nit != g_nodes_by_item.end() ? nit->second : 0);
        }
        if (prop == "SELTEXT") {
            HTREEITEM sel = (HTREEITEM)SendMessageW(h, TVM_GETNEXTITEM, TVGN_CARET, 0);
            return Value::make_string(sel ? tv_item_text(h, sel) : "");
        }
    }
    if (it.kind == "TABS") {
        if (prop == "SELINDEX")
            return Value::make_i64((int)SendMessageW(h, TCM_GETCURSEL, 0, 0));
        if (prop == "COUNT")
            return Value::make_i64((int)SendMessageW(h, TCM_GETITEMCOUNT, 0, 0));
    }
    if (prop == "VALUE" && it.kind == "DATETIME") {
        SYSTEMTIME st = {};
        if (SendMessageW(h, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID)
            return Value::make_f64(systime_to_epoch(st));
        return Value::make_f64(0);
    }
    if (prop == "TEXT")    return Value::make_string(get_hwnd_text(h));
    if (prop == "ENABLED") return Value::make_bool(IsWindowEnabled(h) != 0);
    if (prop == "VISIBLE") return Value::make_bool(IsWindowVisible(h) != 0);
    if (prop == "CHECKED")
        return Value::make_bool(SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (prop == "NAME") return Value::make_string(it.name);
    if (prop == "KIND") return Value::make_string(it.kind);
    if (prop == "HWND") return Value::make_i64((int64_t)(intptr_t)h);
    if (prop == "SELINDEX" && is_list)
        return Value::make_i64((int)SendMessageW(h, it.kind == "COMBO" ? CB_GETCURSEL : LB_GETCURSEL, 0, 0));
    if (prop == "COUNT" && is_list)
        return Value::make_i64((int)SendMessageW(h, it.kind == "COMBO" ? CB_GETCOUNT : LB_GETCOUNT, 0, 0));
    if (prop == "SELTEXT" && is_list) {
        UINT getsel = it.kind == "COMBO" ? CB_GETCURSEL : LB_GETCURSEL;
        UINT gettextlen = it.kind == "COMBO" ? CB_GETLBTEXTLEN : LB_GETTEXTLEN;
        UINT gettext = it.kind == "COMBO" ? CB_GETLBTEXT : LB_GETTEXT;
        int idx = (int)SendMessageW(h, getsel, 0, 0);
        if (idx < 0) return Value::make_string("");
        int len = (int)SendMessageW(h, gettextlen, idx, 0);
        std::wstring w(len + 1, 0);
        SendMessageW(h, gettext, idx, (LPARAM)&w[0]);
        w.resize(len);
        return Value::make_string(narrow(w));
    }
    if (prop == "X" || prop == "Y" || prop == "WIDTH" || prop == "HEIGHT") {
        RECT rc;
        GetWindowRect(h, &rc);
        if (prop == "WIDTH")  return Value::make_i64(lg(rc.right - rc.left));
        if (prop == "HEIGHT") return Value::make_i64(lg(rc.bottom - rc.top));
        POINT tl = { rc.left, rc.top };
        HWND parent = GetParent(h);
        if (parent) ScreenToClient(parent, &tl);
        return Value::make_i64(lg(prop == "X" ? tl.x : tl.y));
    }
    throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                             "' for " + it.kind);
}

// ── .jdform loader ──────────────────────────────────────────────
// The file is JSON: { "version": 1, "form": {...}, "controls": [...] }.
// Controls are created in file order; a "properties" map on an entry is
// applied through the same FORM.SET path afterwards. Handlers named
// <NAME>_<EVENT> that exist in the program are bound automatically.

const Value* jf_get(const Value& obj, const std::string& key) {
    if (obj.type != ValueType::OBJECT) return nullptr;
    return obj.as_object()->get(key);
}

std::string jf_str(const Value& obj, const std::string& key, const std::string& where,
                   const char* deflt = nullptr) {
    const Value* v = jf_get(obj, key);
    if (!v || v->type == ValueType::NONE) {
        if (deflt) return deflt;
        throw std::runtime_error("FORM.LOAD: " + where + ": missing key \"" + key + "\"");
    }
    if (v->type != ValueType::STRING)
        throw std::runtime_error("FORM.LOAD: " + where + ": key \"" + key + "\" must be a string");
    return v->as_string()->data;
}

int jf_int(const Value& obj, const std::string& key, const std::string& where,
           bool required = true, int deflt = 0) {
    const Value* v = jf_get(obj, key);
    if (!v || v->type == ValueType::NONE) {
        if (!required) return deflt;
        throw std::runtime_error("FORM.LOAD: " + where + ": missing key \"" + key + "\"");
    }
    if (v->type == ValueType::STRING || v->type == ValueType::ARRAY ||
        v->type == ValueType::OBJECT)
        throw std::runtime_error("FORM.LOAD: " + where + ": key \"" + key + "\" must be a number");
    return (int)v->to_int();
}

// ── Menu bar builder ────────────────────────────────────────────
// Spec: array of maps. Keys: "text" (required, "-" = separator, "&" marks
// the Alt accelerator), "name" (makes the item clickable -> NAME_CLICK),
// "key" ("Ctrl+O", "F5", ... - real accelerator + right-aligned hint),
// "items" (submenu array).

bool parse_accel_key(const std::string& spec, WORD& fVirt, WORD& vk) {
    fVirt = FVIRTKEY;
    vk = 0;
    std::string token;
    std::vector<std::string> parts;
    for (char ch : spec + "+") {
        if (ch == '+') {
            if (!token.empty()) parts.push_back(upname(token));
            token.clear();
        } else {
            token += ch;
        }
    }
    if (parts.empty()) return false;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (parts[i] == "CTRL" || parts[i] == "CONTROL") fVirt |= FCONTROL;
        else if (parts[i] == "SHIFT") fVirt |= FSHIFT;
        else if (parts[i] == "ALT") fVirt |= FALT;
        else return false;
    }
    const std::string& k = parts.back();
    static const std::map<std::string, WORD> named = {
        {"DEL", VK_DELETE}, {"DELETE", VK_DELETE}, {"INS", VK_INSERT},
        {"INSERT", VK_INSERT}, {"HOME", VK_HOME}, {"END", VK_END},
        {"PGUP", VK_PRIOR}, {"PGDN", VK_NEXT}, {"LEFT", VK_LEFT},
        {"RIGHT", VK_RIGHT}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"ESC", VK_ESCAPE}, {"TAB", VK_TAB}, {"ENTER", VK_RETURN},
        {"RETURN", VK_RETURN}, {"SPACE", VK_SPACE}, {"BACK", VK_BACK}
    };
    auto nit = named.find(k);
    if (nit != named.end()) { vk = nit->second; return true; }
    if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) {
        vk = (WORD)k[0];
        return true;
    }
    if (k.size() >= 2 && k[0] == 'F') {
        int fn = atoi(k.c_str() + 1);
        if (fn >= 1 && fn <= 12) { vk = (WORD)(VK_F1 + fn - 1); return true; }
    }
    return false;
}

void build_menu_level(HMENU parent, const Value& arr, int frm,
                      std::vector<ACCEL>& accels, const char* who);

void build_menu_entry(HMENU parent, const Value& entry, int frm,
                      std::vector<ACCEL>& accels, const char* who) {
    if (entry.type == ValueType::STRING && entry.as_string()->data == "-") {
        AppendMenuW(parent, MF_SEPARATOR, 0, nullptr);
        return;
    }
    if (entry.type != ValueType::OBJECT)
        throw std::runtime_error(std::string(who) + ": each menu entry must be a map or \"-\"");
    const Value* tv = jf_get(entry, "text");
    if (!tv || tv->type != ValueType::STRING)
        throw std::runtime_error(std::string(who) + ": a menu entry needs a \"text\" string");
    std::string text = tv->as_string()->data;
    if (text == "-") {
        AppendMenuW(parent, MF_SEPARATOR, 0, nullptr);
        return;
    }
    const Value* items = jf_get(entry, "items");
    if (items) {
        if (items->type != ValueType::ARRAY)
            throw std::runtime_error(std::string(who) + ": menu \"items\" must be an array");
        HMENU sub = CreatePopupMenu();
        build_menu_level(sub, *items, frm, accels, who);
        AppendMenuW(parent, MF_POPUP | MF_STRING, (UINT_PTR)sub, widen(text).c_str());
        return;
    }
    UINT_PTR id = 0;
    const Value* name = jf_get(entry, "name");
    if (name && name->type == ValueType::STRING) {
        id = (UINT_PTR)store_item(nullptr, name->as_string()->data, "MENU", frm);
    }
    const Value* kv = jf_get(entry, "key");
    std::string key = kv && kv->type == ValueType::STRING ? kv->as_string()->data : "";
    if (!key.empty()) {
        WORD fVirt, vk;
        if (!parse_accel_key(key, fVirt, vk))
            throw std::runtime_error(std::string(who) + ": bad accelerator \"" + key + "\"");
        if (id) accels.push_back({ (BYTE)fVirt, vk, (WORD)id });
        text += "\t" + key;
    }
    AppendMenuW(parent, MF_STRING, id, widen(text).c_str());
}

void build_menu_level(HMENU parent, const Value& arr, int frm,
                      std::vector<ACCEL>& accels, const char* who) {
    for (auto& entry : arr.as_array()->elements)
        build_menu_entry(parent, entry, frm, accels, who);
}

void set_form_menu(int frm, const Value& spec, const char* who) {
    FormsItem& f = form_of(frm, who);
    if (spec.type != ValueType::ARRAY)
        throw std::runtime_error(std::string(who) + ": the menu spec must be an array");

    // rebuild from scratch: drop this form's previous menu items + table
    for (auto it = g_items.begin(); it != g_items.end();) {
        if (it->second.kind == "MENU" && it->second.form == frm) it = g_items.erase(it);
        else ++it;
    }
    if (f.haccel) { DestroyAcceleratorTable(f.haccel); f.haccel = nullptr; }

    std::vector<ACCEL> accels;
    HMENU bar = CreateMenu();
    try {
        build_menu_level(bar, spec, frm, accels, who);
    } catch (...) {
        DestroyMenu(bar);
        throw;
    }
    HMENU old = GetMenu(f.hwnd);
    SetMenu(f.hwnd, bar);
    if (old) DestroyMenu(old);
    DrawMenuBar(f.hwnd);
    if (!accels.empty())
        f.haccel = CreateAcceleratorTableW(accels.data(), (int)accels.size());
}

// ── Popup (context) menu ────────────────────────────────────────
// Same spec as the menu bar, so a context menu is written the way a menu is.
// The items register as ordinary MENU handles, which means WM_COMMAND already
// knows how to dispatch them; the handles from the previous popup on this form
// are dropped first so repeatedly opening one does not accumulate them.
std::map<int, std::vector<int>> g_popup_items;

void show_popup_menu(int frm, const Value& spec, bool at_point, int lx, int ly,
                     const char* who) {
    FormsItem& f = form_of(frm, who);
    if (spec.type != ValueType::ARRAY)
        throw std::runtime_error(std::string(who) + ": the menu spec must be an array");

    for (int h : g_popup_items[frm]) g_items.erase(h);
    g_popup_items[frm].clear();

    std::vector<ACCEL> accels;              // a popup has no accelerator table
    HMENU pop = CreatePopupMenu();
    int first = g_next_handle;
    try {
        build_menu_level(pop, spec, frm, accels, who);
    } catch (...) {
        DestroyMenu(pop);
        throw;
    }
    for (int h = first; h < g_next_handle; h++)
        if (g_items.count(h)) g_popup_items[frm].push_back(h);
    for (int h : g_popup_items[frm]) bind_handlers(g_items[h].name, "MENU");

    POINT pt;
    if (at_point) {
        pt.x = px(lx);
        pt.y = px(ly);
        ClientToScreen(f.hwnd, &pt);
    } else {
        GetCursorPos(&pt);
    }
    // Without TPM_RETURNCMD Windows posts WM_COMMAND to the form, which is the
    // path a menu-bar click already takes.
    SetForegroundWindow(f.hwnd);
    TrackPopupMenu(pop, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, f.hwnd, nullptr);
    DestroyMenu(pop);
}

// ── Toolbar builder ─────────────────────────────────────────────
// Spec: array of maps ("-" = separator). Keys: "name" (required ->
// NAME_CLICK), "text" (tooltip), "icon" (stock icon name), "check"
// (toggle button). Icons come from the common-control standard bitmap.

int stock_icon_index(const std::string& icon) {
    static const std::map<std::string, int> stock = {
        {"CUT", STD_CUT}, {"COPY", STD_COPY}, {"PASTE", STD_PASTE},
        {"UNDO", STD_UNDO}, {"REDO", STD_REDOW}, {"DELETE", STD_DELETE},
        {"NEW", STD_FILENEW}, {"OPEN", STD_FILEOPEN}, {"SAVE", STD_FILESAVE},
        {"PRINTPRE", STD_PRINTPRE}, {"PROPERTIES", STD_PROPERTIES},
        {"HELP", STD_HELP}, {"FIND", STD_FIND}, {"REPLACE", STD_REPLACE},
        {"PRINT", STD_PRINT}
    };
    auto it = stock.find(upname(icon));
    return it == stock.end() ? -1 : it->second;
}

int do_create_toolbar(int frm, const std::string& name, const Value& spec,
                      const char* who) {
    FormsItem& f = form_of(frm, who);
    if (spec.type != ValueType::ARRAY)
        throw std::runtime_error(std::string(who) + ": the toolbar spec must be an array");
    if (toolbar_of(frm))
        throw std::runtime_error(std::string(who) + ": the form already has a toolbar");

    HWND tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                              WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP,
                              0, 0, 0, 0, f.hwnd, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!tb) throw std::runtime_error(std::string(who) + ": toolbar creation failed");
    SendMessageW(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    TBADDBITMAP ab = { HINST_COMMCTRL, IDB_STD_SMALL_COLOR };
    SendMessageW(tb, TB_ADDBITMAP, 0, (LPARAM)&ab);

    std::vector<TBBUTTON> btns;
    for (auto& entry : spec.as_array()->elements) {
        TBBUTTON b = {};
        if (entry.type == ValueType::STRING && entry.as_string()->data == "-") {
            b.fsStyle = BTNS_SEP;
            btns.push_back(b);
            continue;
        }
        if (entry.type != ValueType::OBJECT)
            throw std::runtime_error(std::string(who) + ": each toolbar entry must be a map or \"-\"");
        const Value* nv = jf_get(entry, "name");
        if (!nv || nv->type != ValueType::STRING)
            throw std::runtime_error(std::string(who) + ": a toolbar button needs a \"name\"");
        int id = store_item(nullptr, nv->as_string()->data, "TOOLBTN", frm);
        g_items[id].hwnd = tb;   // buttons act through their toolbar window

        const Value* tv = jf_get(entry, "text");
        if (tv && tv->type == ValueType::STRING)
            g_tooltips[id] = tv->as_string()->data;

        int icon = STD_PROPERTIES;
        const Value* iv = jf_get(entry, "icon");
        if (iv && iv->type == ValueType::STRING) {
            icon = stock_icon_index(iv->as_string()->data);
            if (icon < 0)
                throw std::runtime_error(std::string(who) + ": unknown icon \"" +
                                         iv->as_string()->data + "\"");
        }
        const Value* cv = jf_get(entry, "check");
        b.iBitmap = icon;
        b.idCommand = id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = (BYTE)(cv && cv->to_bool() ? BTNS_CHECK : BTNS_BUTTON);
        btns.push_back(b);
    }
    SendMessageW(tb, TB_ADDBUTTONS, btns.size(), (LPARAM)btns.data());
    int handle = store_item(tb, name, "TOOLBAR", frm);
    layout_form(frm);
    return handle;
}

// ── Status bar builder ──────────────────────────────────────────
// Parts: an array of widths in logical units (-1 = stretch to the right
// edge); anything else creates a single full-width part.

int do_create_statusbar(int frm, const std::string& name, const Value& parts,
                        const char* who) {
    FormsItem& f = form_of(frm, who);
    if (statusbar_of(frm))
        throw std::runtime_error(std::string(who) + ": the form already has a status bar");

    HWND sb = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP | CCS_BOTTOM,
                              0, 0, 0, 0, f.hwnd, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!sb) throw std::runtime_error(std::string(who) + ": status bar creation failed");
    if (g_font) SendMessageW(sb, WM_SETFONT, (WPARAM)g_font, TRUE);

    std::vector<int> edges;
    if (parts.type == ValueType::ARRAY && !parts.as_array()->elements.empty()) {
        int x = 0;
        for (auto& el : parts.as_array()->elements) {
            int w = (int)el.to_int();
            if (w < 0) { edges.push_back(-1); break; }
            x += px(w);
            edges.push_back(x);
        }
    } else {
        edges.push_back(-1);
    }
    SendMessageW(sb, SB_SETPARTS, edges.size(), (LPARAM)edges.data());

    int handle = store_item(sb, name, "STATUSBAR", frm);
    layout_form(frm);
    return handle;
}

// Event names a control kind can fire; used for automatic handler binding.
const std::vector<const char*>& events_of(const std::string& kind) {
    // Focus, keyboard and mouse ride on every control that can take input, so
    // a .jdform binds them without an ON the way the rest already works.
#define JDF_INPUT_EV "GOTFOCUS", "LOSTFOCUS", "KEYDOWN", "KEYUP", "KEYPRESS", \
                     "MOUSEDOWN", "MOUSEUP", "MOUSEMOVE"
    static const std::vector<const char*> form_ev  = { "LOAD", "UNLOAD", "RESIZE",
                                                       "MOUSEDOWN", "MOUSEUP", "MOUSEMOVE" };
    static const std::vector<const char*> click_ev = { "CLICK", JDF_INPUT_EV };
    static const std::vector<const char*> text_ev  = { "CHANGE", JDF_INPUT_EV };
    static const std::vector<const char*> list_ev  = { "CLICK", "DBLCLICK", JDF_INPUT_EV };
    static const std::vector<const char*> tick_ev  = { "TICK" };
    static const std::vector<const char*> none_ev  = {};
#undef JDF_INPUT_EV
    if (is_form_kind(kind)) return form_ev;
    if (kind == "BUTTON" || kind == "CHECKBOX" || kind == "RADIO" ||
        kind == "MENU" || kind == "TOOLBTN") return click_ev;
    if (kind == "TEXTBOX" || kind == "COMBO" || kind == "SLIDER" ||
        kind == "UPDOWN" || kind == "TABS" || kind == "DATETIME" ||
        kind == "RICHTEXT")  return text_ev;
    if (kind == "LISTBOX" || kind == "LISTVIEW" || kind == "TREEVIEW") return list_ev;
    if (kind == "TIMER")    return tick_ev;
    return none_ev;
}

void bind_handlers(const std::string& ctl_name, const std::string& kind) {
    for (const char* ev : events_of(kind)) {
        std::string handler = ctl_name + "_" + ev;
        // Native mode (a user_event_dispatch is installed): the compiled
        // handler bodies are invisible to this bridge VM, so bind every
        // candidate - the exe-side trampoline drops events for which no
        // SUB was registered.
        if (g_vm->user_event_dispatch) {
            g_vm->event_on(handler, handler);
            continue;
        }
        if (!g_vm->is_native(handler) && g_vm->function_exists(handler))
            g_vm->event_on(handler, handler);
    }
}

void load_controls(int frm, const Value& doc, const std::string& path) {
    const Value* controls = jf_get(doc, "controls");
    if (controls && controls->type != ValueType::ARRAY)
        throw std::runtime_error("FORM.LOAD: " + path + ": \"controls\" must be an array");
    if (!controls) return;

    auto& elems = controls->as_array()->elements;
    for (size_t i = 0; i < elems.size(); i++) {
        std::string where = "controls[" + std::to_string(i) + "]";
        const Value& c = elems[i];
        if (c.type != ValueType::OBJECT)
            throw std::runtime_error("FORM.LOAD: " + where + ": must be an object");

        std::string type = upname(jf_str(c, "type", where));
        std::string name = jf_str(c, "name", where);
        where += " (" + name + ")";
        std::string text = jf_str(c, "text", where, "");
        int x = jf_int(c, "x", where, type != "TIMER");
        int y = jf_int(c, "y", where, type != "TIMER");
        int w = jf_int(c, "w", where, type != "TIMER");
        int h = jf_int(c, "h", where, type != "TIMER");

        int handle;
        if (type == "BUTTON") {
            handle = create_control(frm, name, L"BUTTON", text,
                                    BS_PUSHBUTTON | WS_TABSTOP, 0, x, y, w, h, "BUTTON");
        } else if (type == "LABEL") {
            handle = create_control(frm, name, L"STATIC", text, SS_LEFT, 0,
                                    x, y, w, h, "LABEL");
        } else if (type == "TEXTBOX") {
            bool multi = jf_int(c, "multiline", where, false) != 0;
            DWORD style = multi
                ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP
                : ES_AUTOHSCROLL | WS_TABSTOP;
            handle = create_control(frm, name, L"EDIT", text, style, WS_EX_CLIENTEDGE,
                                    x, y, w, h, "TEXTBOX");
        } else if (type == "CHECKBOX") {
            handle = create_control(frm, name, L"BUTTON", text,
                                    BS_AUTOCHECKBOX | WS_TABSTOP, 0, x, y, w, h, "CHECKBOX");
        } else if (type == "RADIO") {
            DWORD style = BS_AUTORADIOBUTTON | WS_TABSTOP;
            if (jf_int(c, "new_group", where, false) != 0) style |= WS_GROUP;
            handle = create_control(frm, name, L"BUTTON", text, style, 0,
                                    x, y, w, h, "RADIO");
        } else if (type == "FRAME") {
            handle = create_control(frm, name, L"BUTTON", text, BS_GROUPBOX, 0,
                                    x, y, w, h, "FRAME");
        } else if (type == "LISTBOX") {
            handle = create_control(frm, name, L"LISTBOX", "",
                                    LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                                    x, y, w, h, "LISTBOX");
        } else if (type == "COMBO") {
            handle = create_control(frm, name, L"COMBOBOX", "",
                                    CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                                    x, y, w, h + 240, "COMBO");
        } else if (type == "TIMER") {
            FormsItem& f = form_of(frm, "FORM.LOAD");
            handle = store_item(nullptr, name, "TIMER", frm);
            int ms = jf_int(c, "interval", where, false);
            if (ms > 0) SetTimer(f.hwnd, handle, ms, nullptr);
        } else if (type == "LINE") {
            bool vert = jf_int(c, "vertical", where, false) != 0;
            handle = create_control(frm, name, L"STATIC", "",
                                    vert ? SS_ETCHEDVERT : SS_ETCHEDHORZ, 0,
                                    x, y, vert ? 2 : w, vert ? h : 2, "LINE");
        } else if (type == "SHAPE") {
            handle = create_control(frm, name, L"STATIC", "", SS_NOTIFY, 0,
                                    x, y, w, h, "SHAPE");
            int rgb = jf_int(c, "color", where, false, 0x808080);
            g_shape_colors[handle] = rgb;
            g_shape_brushes[handle] = CreateSolidBrush(rgb_to_colorref(rgb));
        } else if (type == "PICTURE") {
            handle = create_control(frm, name, L"STATIC", "",
                                    SS_BITMAP | SS_CENTERIMAGE, 0, x, y, w, h, "PICTURE");
            std::string ppath = resolve_data_path(jf_str(c, "picture", where, ""));
            if (!ppath.empty()) {
                HBITMAP bmp = load_image_file(widen(ppath), px(w), px(h));
                if (!bmp)
                    throw std::runtime_error("FORM.LOAD: " + where +
                                             ": cannot load image \"" + ppath + "\"");
                g_pictures[handle] = bmp;
                SendMessageW(g_items[handle].hwnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp);
            }
        } else if (type == "PROGRESS") {
            handle = create_control(frm, name, PROGRESS_CLASSW, "", 0, 0,
                                    x, y, w, h, "PROGRESS");
            SendMessageW(g_items[handle].hwnd, PBM_SETRANGE32, 0,
                         jf_int(c, "max", where, false, 100));
        } else if (type == "SLIDER") {
            bool vert = jf_int(c, "vertical", where, false) != 0;
            handle = create_control(frm, name, TRACKBAR_CLASSW, "",
                                    TBS_AUTOTICKS | WS_TABSTOP | (vert ? TBS_VERT : TBS_HORZ),
                                    0, x, y, w, h, "SLIDER");
            SendMessageW(g_items[handle].hwnd, TBM_SETRANGEMAX, TRUE,
                         jf_int(c, "max", where, false, 100));
        } else if (type == "UPDOWN") {
            handle = (int)g_vm->call_function("FORM.UPDOWN", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h),
                Value::make_i64(jf_int(c, "min", where, false, 0)),
                Value::make_i64(jf_int(c, "max", where, false, 100)) }).to_int();
        } else if (type == "LISTVIEW") {
            handle = (int)g_vm->call_function("FORM.LISTVIEW", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h) }).to_int();
        } else if (type == "TREEVIEW") {
            handle = (int)g_vm->call_function("FORM.TREEVIEW", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h) }).to_int();
        } else if (type == "TABS") {
            const Value* titles = jf_get(c, "titles");
            handle = (int)g_vm->call_function("FORM.TABS", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h),
                titles ? *titles : Value::make_array() }).to_int();
        } else if (type == "DATETIME") {
            handle = (int)g_vm->call_function("FORM.DATETIME", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h) }).to_int();
        } else if (type == "RICHTEXT") {
            handle = (int)g_vm->call_function("FORM.RICHTEXT", {
                Value::make_i64(frm), Value::make_string(name),
                Value::make_i64(x), Value::make_i64(y),
                Value::make_i64(w), Value::make_i64(h) }).to_int();
            std::string text0 = jf_str(c, "text", where, "");
            if (!text0.empty())
                SetWindowTextW(g_items[handle].hwnd, widen(text0).c_str());
        } else {
            throw std::runtime_error("FORM.LOAD: " + where + ": unknown type \"" + type + "\"");
        }

        const Value* props = jf_get(c, "properties");
        if (props) {
            if (props->type != ValueType::OBJECT)
                throw std::runtime_error("FORM.LOAD: " + where + ": \"properties\" must be an object");
            for (auto& [pkey, pval] : props->as_object()->fields) {
                try {
                    set_prop(handle, g_items[handle], upname(pkey), pval, "FORM.LOAD");
                } catch (const std::exception& e) {
                    throw std::runtime_error("FORM.LOAD: " + where + ": property \"" +
                                             pkey + "\": " + e.what());
                }
            }
        }

        // "tab": "<tabs name>", "page": n puts the control on a tab page
        const Value* tabref = jf_get(c, "tab");
        if (tabref && tabref->type == ValueType::STRING) {
            std::string tname = upname(tabref->as_string()->data);
            int page = jf_int(c, "page", where, false, 0);
            int tabh = 0;
            for (auto& [h2, it2] : g_items)
                if (it2.kind == "TABS" && it2.form == frm && it2.name == tname) {
                    tabh = h2;
                    break;
                }
            if (!tabh)
                throw std::runtime_error("FORM.LOAD: " + where + ": unknown tab \"" +
                                         tabref->as_string()->data + "\"");
            g_tab_pages[tabh][page].push_back(handle);
            apply_tab_visibility(tabh);
        }

        bind_handlers(g_items[handle].name, g_items[handle].kind);
    }
}

int load_jdform(const std::string& path, int mdi_parent) {
    // Resolve like the graphics loaders: as given first, then against the
    // script's own directory.
    std::string resolved = path;
    DWORD attr = GetFileAttributesW(widen(resolved).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES && !g_base_dir.empty()) {
        std::string alt = g_base_dir + "/" + path;
        if (GetFileAttributesW(widen(alt).c_str()) != INVALID_FILE_ATTRIBUTES)
            resolved = alt;
    }

    Value text = g_vm->call_function("TXTREADER$", { Value::make_string(resolved) });
    Value doc  = g_vm->call_function("JSON.PARSE$", { text });
    if (doc.type != ValueType::OBJECT)
        throw std::runtime_error("FORM.LOAD: " + path + ": top level must be a JSON object");

    int version = jf_int(doc, "version", "top level");
    if (version != 1)
        throw std::runtime_error("FORM.LOAD: " + path + ": unsupported version " +
                                 std::to_string(version) + " (this runtime reads version 1)");

    const Value* fdef = jf_get(doc, "form");
    if (!fdef || fdef->type != ValueType::OBJECT)
        throw std::runtime_error("FORM.LOAD: " + path + ": missing \"form\" object");

    std::string fname = upname(jf_str(*fdef, "name", "form"));
    const Value* mv = jf_get(*fdef, "mdi");
    int frm;
    if (mdi_parent != 0) {
        frm = do_create_child(mdi_parent, jf_str(*fdef, "title", "form", ""),
                              jf_int(*fdef, "width", "form"),
                              jf_int(*fdef, "height", "form"), fname);
    } else {
        frm = do_create_form(jf_str(*fdef, "title", "form", ""),
                             jf_int(*fdef, "width", "form"),
                             jf_int(*fdef, "height", "form"), fname,
                             mv && mv->to_bool());
    }
    // Optional fixed position for a top-level form; centered otherwise.
    if (mdi_parent == 0 && (jf_get(*fdef, "x") || jf_get(*fdef, "y"))) {
        SetWindowPos(g_items[frm].hwnd, nullptr,
                     px(jf_int(*fdef, "x", "form", false, 0)),
                     px(jf_int(*fdef, "y", "form", false, 0)),
                     0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // A bad control entry must not leave the half-built (still invisible)
    // form behind - it would count as open forever.
    try {
        bind_handlers(fname, g_items[frm].kind);
        const Value* menu = jf_get(*fdef, "menu");
        if (menu) {
            set_form_menu(frm, *menu, "FORM.LOAD");
            for (auto& [h, it] : g_items)
                if (it.kind == "MENU" && it.form == frm)
                    bind_handlers(it.name, "MENU");
        }
        const Value* toolbar = jf_get(*fdef, "toolbar");
        if (toolbar) {
            do_create_toolbar(frm, fname + "_TB", *toolbar, "FORM.LOAD");
            for (auto& [h, it] : g_items)
                if (it.kind == "TOOLBTN" && it.form == frm)
                    bind_handlers(it.name, "TOOLBTN");
        }
        const Value* statusbar = jf_get(*fdef, "statusbar");
        if (statusbar && statusbar->to_bool())
            do_create_statusbar(frm, fname + "_SB", *statusbar, "FORM.LOAD");
        load_controls(frm, doc, path);
    } catch (...) {
        DestroyWindow(g_items[frm].hwnd);
        throw;
    }
    return frm;
}

} // namespace

// ── Registration ────────────────────────────────────────────────

void register_forms_builtins(VM& vm) {
    g_vm = &vm;

    // FORM.SET carries whole-array payloads (ITEMS) and every creation
    // call takes coordinate lists a user might compute as vectors; none
    // of them may be auto-vectorized into per-element calls.
    for (const char* n : { "FORM.CREATE", "FORM.BUTTON", "FORM.LABEL", "FORM.TEXTBOX",
                           "FORM.CHECKBOX", "FORM.RADIO", "FORM.FRAME", "FORM.LISTBOX",
                           "FORM.COMBO", "FORM.TIMER", "FORM.LOAD", "FORM.FIND",
                           "FORM.MDI", "FORM.CHILD", "FORM.MENU", "FORM.POPUP", "FORM.TOOLBAR",
                           "FORM.STATUSBAR",
                           "FORM.LINE", "FORM.SHAPE", "FORM.PICTURE", "FORM.PROGRESS",
                           "FORM.SLIDER", "FORM.UPDOWN",
                           "FORM.LISTVIEW", "FORM.TREEVIEW", "FORM.NODE",
                           "FORM.TABS", "FORM.TABPAGE", "FORM.DATETIME", "FORM.RICHTEXT",
                           "FILEOPEN$", "FILESAVE$", "COLORDIALOG", "FONTDIALOG$",
                           "FORM.SET", "FORM.GET",
                           "FORM.SHOW", "FORM.RUN", "FORM.CLOSE", "FORM.DOEVENTS",
                           "MSGBOX", "INPUTBOX$" })
        vm.extra_no_vectorize.insert(n);

    // FORM.CREATE(title$, width, height, [name$]) -> handle
    vm.register_native("FORM.CREATE", 3, 4, [](const std::vector<Value>& args) -> Value {
        std::string name = args.size() > 3 ? arg_str(args, 3, "FORM.CREATE")
                                           : "FORM" + std::to_string(g_next_handle);
        return Value::make_i64(do_create_form(arg_str(args, 0, "FORM.CREATE"),
                                              arg_int(args, 1), arg_int(args, 2), name));
    });

    // FORM.BUTTON(frm, name$, caption$, x, y, w, h) -> handle
    vm.register_native("FORM.BUTTON", 7, 7, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.BUTTON"), L"BUTTON",
            arg_str(args, 2, "FORM.BUTTON"), BS_PUSHBUTTON | WS_TABSTOP, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "BUTTON"));
    });

    // FORM.LABEL(frm, name$, caption$, x, y, w, h) -> handle
    vm.register_native("FORM.LABEL", 7, 7, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.LABEL"), L"STATIC",
            arg_str(args, 2, "FORM.LABEL"), SS_LEFT, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "LABEL"));
    });

    // FORM.TEXTBOX(frm, name$, text$, x, y, w, h, [multiline]) -> handle
    vm.register_native("FORM.TEXTBOX", 7, 8, [](const std::vector<Value>& args) -> Value {
        DWORD style = ES_AUTOHSCROLL | WS_TABSTOP;
        if (args.size() > 7 && args[7].to_bool())
            style = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP;
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.TEXTBOX"), L"EDIT",
            arg_str(args, 2, "FORM.TEXTBOX"), style, WS_EX_CLIENTEDGE,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "TEXTBOX"));
    });

    // FORM.CHECKBOX(frm, name$, caption$, x, y, w, h) -> handle
    vm.register_native("FORM.CHECKBOX", 7, 7, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.CHECKBOX"), L"BUTTON",
            arg_str(args, 2, "FORM.CHECKBOX"), BS_AUTOCHECKBOX | WS_TABSTOP, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "CHECKBOX"));
    });

    // FORM.RADIO(frm, name$, caption$, x, y, w, h, [new_group]) -> handle
    vm.register_native("FORM.RADIO", 7, 8, [](const std::vector<Value>& args) -> Value {
        DWORD style = BS_AUTORADIOBUTTON | WS_TABSTOP;
        if (args.size() > 7 && args[7].to_bool()) style |= WS_GROUP;
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.RADIO"), L"BUTTON",
            arg_str(args, 2, "FORM.RADIO"), style, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "RADIO"));
    });

    // FORM.FRAME(frm, name$, caption$, x, y, w, h) -> handle
    vm.register_native("FORM.FRAME", 7, 7, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.FRAME"), L"BUTTON",
            arg_str(args, 2, "FORM.FRAME"), BS_GROUPBOX, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "FRAME"));
    });

    // FORM.LISTBOX(frm, name$, x, y, w, h) -> handle
    vm.register_native("FORM.LISTBOX", 6, 6, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.LISTBOX"), L"LISTBOX", "",
            LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "LISTBOX"));
    });

    // FORM.COMBO(frm, name$, x, y, w, h) -> handle
    vm.register_native("FORM.COMBO", 6, 6, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.COMBO"), L"COMBOBOX", "",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
            // the Win32 combo height covers the closed box plus the open
            // drop-down list, so give the list room below the visible part
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5) + 240, "COMBO"));
    });

    // FORM.TIMER(frm, name$, interval_ms) -> handle  (0 = created disabled)
    vm.register_native("FORM.TIMER", 3, 3, [](const std::vector<Value>& args) -> Value {
        FormsItem& f = form_of(arg_int(args, 0), "FORM.TIMER");
        std::string name = arg_str(args, 1, "FORM.TIMER");
        int ms = arg_int(args, 2);
        int handle = store_item(nullptr, name, "TIMER", arg_int(args, 0));
        bind_handlers(g_items[handle].name, "TIMER");
        if (ms > 0) SetTimer(f.hwnd, handle, ms, nullptr);
        return Value::make_i64(handle);
    });

    // FORM.MDI(title$, width, height, [name$]) -> handle  MDI frame window
    vm.register_native("FORM.MDI", 3, 4, [](const std::vector<Value>& args) -> Value {
        std::string name = args.size() > 3 ? arg_str(args, 3, "FORM.MDI")
                                           : "MDI" + std::to_string(g_next_handle);
        return Value::make_i64(do_create_form(arg_str(args, 0, "FORM.MDI"),
                                              arg_int(args, 1), arg_int(args, 2),
                                              name, true));
    });

    // FORM.CHILD(frame, title$, width, height, [name$]) -> handle
    vm.register_native("FORM.CHILD", 4, 5, [](const std::vector<Value>& args) -> Value {
        std::string name = args.size() > 4 ? arg_str(args, 4, "FORM.CHILD")
                                           : "CHILD" + std::to_string(g_next_handle);
        return Value::make_i64(do_create_child(arg_int(args, 0),
                                               arg_str(args, 1, "FORM.CHILD"),
                                               arg_int(args, 2), arg_int(args, 3), name));
    });

    // FORM.MENU(frm, spec)  build/replace the form's menu bar; every item
    // with a name dispatches NAME_CLICK, handlers are bound automatically
    vm.register_native("FORM.MENU", 2, 2, [](const std::vector<Value>& args) -> Value {
        int frm = arg_int(args, 0);
        set_form_menu(frm, args[1], "FORM.MENU");
        for (auto& [h, it] : g_items)
            if (it.kind == "MENU" && it.form == frm)
                bind_handlers(it.name, "MENU");
        return Value::make_none();
    });

    // FORM.POPUP(frm, spec, [x], [y])  context menu; without coordinates it
    // opens where the mouse is, which is what a right-click handler wants
    vm.register_native("FORM.POPUP", 2, 4, [](const std::vector<Value>& args) -> Value {
        int frm = arg_int(args, 0);
        bool at_point = args.size() >= 4;
        int x = at_point ? arg_int(args, 2) : 0;
        int y = at_point ? arg_int(args, 3) : 0;
        show_popup_menu(frm, args[1], at_point, x, y, "FORM.POPUP");
        return Value::make_none();
    });

    // FORM.TOOLBAR(frm, name$, spec) -> handle  flat icon toolbar along the
    // top; buttons dispatch NAME_CLICK, handlers are bound automatically
    vm.register_native("FORM.TOOLBAR", 3, 3, [](const std::vector<Value>& args) -> Value {
        int frm = arg_int(args, 0);
        int handle = do_create_toolbar(frm, arg_str(args, 1, "FORM.TOOLBAR"),
                                       args[2], "FORM.TOOLBAR");
        for (auto& [h, it] : g_items)
            if (it.kind == "TOOLBTN" && it.form == frm)
                bind_handlers(it.name, "TOOLBTN");
        return Value::make_i64(handle);
    });

    // FORM.STATUSBAR(frm, name$, [part_widths]) -> handle  status bar along
    // the bottom; TEXT takes a string (part 0) or an array (all parts)
    vm.register_native("FORM.STATUSBAR", 2, 3, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(do_create_statusbar(
            arg_int(args, 0), arg_str(args, 1, "FORM.STATUSBAR"),
            args.size() > 2 ? args[2] : Value::make_none(), "FORM.STATUSBAR"));
    });

    // FORM.LOAD(path$, [mdi_frame]) -> handle  instantiate a .jdform file
    // (as an MDI child of mdi_frame if given) and bind every
    // <NAME>_<EVENT> handler that exists in the program
    vm.register_native("FORM.LOAD", 1, 2, [](const std::vector<Value>& args) -> Value {
        int parent = args.size() > 1 ? arg_int(args, 1) : 0;
        return Value::make_i64(load_jdform(arg_str(args, 0, "FORM.LOAD"), parent));
    });

    // FORM.FIND(frm, name$) -> handle  look up a control by name (0 if absent)
    vm.register_native("FORM.FIND", 2, 2, [](const std::vector<Value>& args) -> Value {
        int frm = arg_int(args, 0);
        form_of(frm, "FORM.FIND");
        std::string name = upname(arg_str(args, 1, "FORM.FIND"));
        for (auto& [h, it] : g_items)
            if (it.form == frm && it.name == name) return Value::make_i64(h);
        return Value::make_i64(0);
    });

    // FORM.LINE(frm, name$, x, y, length, [vertical]) -> handle
    // The classic etched divider.
    vm.register_native("FORM.LINE", 5, 6, [](const std::vector<Value>& args) -> Value {
        bool vert = args.size() > 5 && args[5].to_bool();
        int len = arg_int(args, 4);
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.LINE"), L"STATIC", "",
            vert ? SS_ETCHEDVERT : SS_ETCHEDHORZ, 0,
            arg_int(args, 2), arg_int(args, 3),
            vert ? 2 : len, vert ? len : 2, "LINE"));
    });

    // FORM.SHAPE(frm, name$, x, y, w, h, [rgb]) -> handle  filled rectangle
    vm.register_native("FORM.SHAPE", 6, 7, [](const std::vector<Value>& args) -> Value {
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.SHAPE"), L"STATIC", "",
            SS_NOTIFY, 0,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "SHAPE");
        int rgb = args.size() > 6 ? (int)args[6].to_int() : 0x808080;
        g_shape_colors[handle] = rgb;
        g_shape_brushes[handle] = CreateSolidBrush(rgb_to_colorref(rgb));
        InvalidateRect(g_items[handle].hwnd, nullptr, TRUE);
        return Value::make_i64(handle);
    });

    // FORM.PICTURE(frm, name$, path$, x, y, w, h) -> handle
    // PNG/JPG/BMP via WIC, scaled to the control size.
    vm.register_native("FORM.PICTURE", 7, 7, [](const std::vector<Value>& args) -> Value {
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.PICTURE"), L"STATIC", "",
            SS_BITMAP | SS_CENTERIMAGE, 0,
            arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), arg_int(args, 6), "PICTURE");
        std::string path = resolve_data_path(arg_str(args, 2, "FORM.PICTURE"));
        HBITMAP bmp = load_image_file(widen(path),
                                      px(arg_int(args, 5)), px(arg_int(args, 6)));
        if (!bmp)
            throw std::runtime_error("FORM.PICTURE: cannot load image '" + path + "'");
        g_pictures[handle] = bmp;
        SendMessageW(g_items[handle].hwnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp);
        return Value::make_i64(handle);
    });

    // FORM.PROGRESS(frm, name$, x, y, w, h) -> handle  (range 0..100)
    vm.register_native("FORM.PROGRESS", 6, 6, [](const std::vector<Value>& args) -> Value {
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.PROGRESS"), PROGRESS_CLASSW, "",
            0, 0,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "PROGRESS");
        SendMessageW(g_items[handle].hwnd, PBM_SETRANGE32, 0, 100);
        return Value::make_i64(handle);
    });

    // FORM.SLIDER(frm, name$, x, y, w, h, [max=100], [vertical]) -> handle
    vm.register_native("FORM.SLIDER", 6, 8, [](const std::vector<Value>& args) -> Value {
        bool vert = args.size() > 7 && args[7].to_bool();
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.SLIDER"), TRACKBAR_CLASSW, "",
            TBS_AUTOTICKS | WS_TABSTOP | (vert ? TBS_VERT : TBS_HORZ), 0,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "SLIDER");
        int maxv = args.size() > 6 ? (int)args[6].to_int() : 100;
        SendMessageW(g_items[handle].hwnd, TBM_SETRANGEMAX, TRUE, maxv);
        return Value::make_i64(handle);
    });

    // FORM.UPDOWN(frm, name$, x, y, w, h, [min=0], [max=100]) -> handle
    // A numeric edit with spin arrows.
    vm.register_native("FORM.UPDOWN", 6, 8, [](const std::vector<Value>& args) -> Value {
        int frm = arg_int(args, 0);
        FormsItem& f = form_of(frm, "FORM.UPDOWN");
        std::string name = arg_str(args, 1, "FORM.UPDOWN");
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                                    px(arg_int(args, 2)), px(arg_int(args, 3)),
                                    px(arg_int(args, 4)), px(arg_int(args, 5)),
                                    f.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!edit) throw std::runtime_error("FORM.UPDOWN: edit creation failed");
        if (g_font) SendMessageW(edit, WM_SETFONT, (WPARAM)g_font, TRUE);
        HWND ud = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
                                  WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT |
                                  UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                  0, 0, 0, 0, f.hwnd, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
        if (!ud) { DestroyWindow(edit); throw std::runtime_error("FORM.UPDOWN: creation failed"); }
        SendMessageW(ud, UDM_SETBUDDY, (WPARAM)edit, 0);
        int lo = args.size() > 6 ? (int)args[6].to_int() : 0;
        int hi = args.size() > 7 ? (int)args[7].to_int() : 100;
        SendMessageW(ud, UDM_SETRANGE32, lo, hi);
        SendMessageW(ud, UDM_SETPOS32, 0, lo);
        int handle = store_item(ud, name, "UPDOWN", frm);
        g_items[handle].buddy = edit;
        g_byhwnd[edit] = handle;   // EN_CHANGE arrives with the buddy hwnd
        bind_handlers(g_items[handle].name, "UPDOWN");
        return Value::make_i64(handle);
    });

    // FORM.LISTVIEW(frm, name$, x, y, w, h) -> handle  report-mode list
    // with full-row select and grid lines; fill via COLUMNS + ITEMS.
    vm.register_native("FORM.LISTVIEW", 6, 6, [](const std::vector<Value>& args) -> Value {
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.LISTVIEW"), WC_LISTVIEWW, "",
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "LISTVIEW");
        SendMessageW(g_items[handle].hwnd, LVM_SETEXTENDEDLISTVIEWSTYLE,
                     LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES,
                     LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        return Value::make_i64(handle);
    });

    // FORM.TREEVIEW(frm, name$, x, y, w, h) -> handle
    vm.register_native("FORM.TREEVIEW", 6, 6, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.TREEVIEW"), WC_TREEVIEWW, "",
            TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | WS_TABSTOP,
            WS_EX_CLIENTEDGE,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "TREEVIEW"));
    });

    // FORM.NODE(tree, parent_node, text$) -> node id  (parent 0 = root)
    vm.register_native("FORM.NODE", 3, 3, [](const std::vector<Value>& args) -> Value {
        FormsItem& it = item_of(arg_int(args, 0), "FORM.NODE");
        if (it.kind != "TREEVIEW")
            throw std::runtime_error("FORM.NODE: handle is not a treeview");
        HTREEITEM parent = TVI_ROOT;
        int pid = arg_int(args, 1);
        if (pid != 0) {
            auto pit = g_tree_nodes.find(pid);
            if (pit == g_tree_nodes.end())
                throw std::runtime_error("FORM.NODE: unknown parent node " + std::to_string(pid));
            parent = pit->second.hitem;
        }
        std::wstring text = widen(arg_str(args, 2, "FORM.NODE"));
        TVINSERTSTRUCTW ins = {};
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT;
        ins.item.pszText = (LPWSTR)text.c_str();
        HTREEITEM hitem = (HTREEITEM)SendMessageW(it.hwnd, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        if (!hitem) throw std::runtime_error("FORM.NODE: insert failed");
        int id = g_next_handle++;
        g_tree_nodes[id] = { hitem, arg_int(args, 0) };
        g_nodes_by_item[hitem] = id;
        return Value::make_i64(id);
    });

    // FORM.TABS(frm, name$, x, y, w, h, titles) -> handle
    vm.register_native("FORM.TABS", 7, 7, [](const std::vector<Value>& args) -> Value {
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.TABS"), WC_TABCONTROLW, "",
            WS_TABSTOP | WS_CLIPSIBLINGS, 0,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "TABS");
        if (args[6].type == ValueType::ARRAY) {
            auto& els = args[6].as_array()->elements;
            for (size_t i = 0; i < els.size(); i++) {
                std::wstring t = widen(value_text(els[i]));
                TCITEMW item = {};
                item.mask = TCIF_TEXT;
                item.pszText = (LPWSTR)t.c_str();
                SendMessageW(g_items[handle].hwnd, TCM_INSERTITEMW, i, (LPARAM)&item);
            }
        }
        return Value::make_i64(handle);
    });

    // FORM.TABPAGE(tab, page, ctl)  assign a control to a tab page;
    // visibility follows the selected tab from then on
    vm.register_native("FORM.TABPAGE", 3, 3, [](const std::vector<Value>& args) -> Value {
        int tab = arg_int(args, 0);
        FormsItem& it = item_of(tab, "FORM.TABPAGE");
        if (it.kind != "TABS")
            throw std::runtime_error("FORM.TABPAGE: handle is not a tab control");
        item_of(arg_int(args, 2), "FORM.TABPAGE");
        g_tab_pages[tab][arg_int(args, 1)].push_back(arg_int(args, 2));
        apply_tab_visibility(tab);
        return Value::make_none();
    });

    // FORM.DATETIME(frm, name$, x, y, w, h) -> handle  date picker
    vm.register_native("FORM.DATETIME", 6, 6, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.DATETIME"), DATETIMEPICK_CLASSW, "",
            DTS_SHORTDATEFORMAT | WS_TABSTOP, 0,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "DATETIME"));
    });

    // FORM.RICHTEXT(frm, name$, x, y, w, h) -> handle  rich edit
    vm.register_native("FORM.RICHTEXT", 6, 6, [](const std::vector<Value>& args) -> Value {
        static bool loaded = false;
        if (!loaded) {
            LoadLibraryW(L"Msftedit.dll");
            loaded = true;
        }
        int handle = create_control(
            arg_int(args, 0), arg_str(args, 1, "FORM.RICHTEXT"), L"RICHEDIT50W", "",
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP,
            WS_EX_CLIENTEDGE,
            arg_int(args, 2), arg_int(args, 3), arg_int(args, 4), arg_int(args, 5), "RICHTEXT");
        SendMessageW(g_items[handle].hwnd, EM_SETEVENTMASK, 0, ENM_CHANGE);
        return Value::make_i64(handle);
    });

    // FILEOPEN$([filter$], [title$], [initial_dir$]) -> path ("" = cancel)
    // Filter format: "Text files|*.txt|All files|*.*"
    vm.register_native("FILEOPEN$", 0, 3, [](const std::vector<Value>& args) -> Value {
        std::wstring filter = widen(args.size() > 0 && args[0].type == ValueType::STRING &&
                                    !args[0].as_string()->data.empty()
                                    ? args[0].as_string()->data : "All files|*.*");
        for (auto& c : filter) if (c == L'|') c = L'\0';
        filter.push_back(L'\0');
        std::wstring title = args.size() > 1 ? widen(value_text(args[1])) : L"";
        std::wstring dir   = args.size() > 2 ? widen(value_text(args[2])) : L"";
        wchar_t buf[4096] = L"";
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = buf;
        ofn.nMaxFile = 4096;
        if (!title.empty()) ofn.lpstrTitle = title.c_str();
        if (!dir.empty())   ofn.lpstrInitialDir = dir.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return Value::make_string("");
        return Value::make_string(narrow(buf));
    });

    // FILESAVE$([filter$], [title$], [default_name$]) -> path ("" = cancel)
    vm.register_native("FILESAVE$", 0, 3, [](const std::vector<Value>& args) -> Value {
        std::wstring filter = widen(args.size() > 0 && args[0].type == ValueType::STRING &&
                                    !args[0].as_string()->data.empty()
                                    ? args[0].as_string()->data : "All files|*.*");
        for (auto& c : filter) if (c == L'|') c = L'\0';
        filter.push_back(L'\0');
        std::wstring title = args.size() > 1 ? widen(value_text(args[1])) : L"";
        wchar_t buf[4096] = L"";
        if (args.size() > 2) {
            std::wstring def = widen(value_text(args[2]));
            wcsncpy(buf, def.c_str(), 4095);
        }
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = buf;
        ofn.nMaxFile = 4096;
        if (!title.empty()) ofn.lpstrTitle = title.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) return Value::make_string("");
        return Value::make_string(narrow(buf));
    });

    // COLORDIALOG([initial_rgb]) -> 0xRRGGBB, or -1 on cancel
    vm.register_native("COLORDIALOG", 0, 1, [](const std::vector<Value>& args) -> Value {
        static COLORREF custom[16] = {};
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_ANYCOLOR;
        if (!args.empty()) {
            cc.rgbResult = rgb_to_colorref((int)args[0].to_int());
            cc.Flags |= CC_RGBINIT;
        }
        if (!ChooseColorW(&cc)) return Value::make_i64(-1);
        int rgb = (GetRValue(cc.rgbResult) << 16) |
                  (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
        return Value::make_i64(rgb);
    });

    // FONTDIALOG$([face$], [size]) -> "face,size,bold,italic" ("" = cancel)
    vm.register_native("FONTDIALOG$", 0, 2, [](const std::vector<Value>& args) -> Value {
        LOGFONTW lf = {};
        HDC dc = GetDC(nullptr);
        int logy = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(nullptr, dc);
        std::wstring face = args.size() > 0 ? widen(value_text(args[0])) : L"Segoe UI";
        int size = args.size() > 1 ? (int)args[1].to_int() : 9;
        wcsncpy(lf.lfFaceName, face.c_str(), LF_FACESIZE - 1);
        lf.lfHeight = -MulDiv(size, logy, 72);
        CHOOSEFONTW cf = { sizeof(cf) };
        cf.hwndOwner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
        if (!ChooseFontW(&cf)) return Value::make_string("");
        int pt = MulDiv(-lf.lfHeight, 72, logy);
        std::string out = narrow(lf.lfFaceName) + "," + std::to_string(pt) + "," +
                          (lf.lfWeight >= FW_BOLD ? "1" : "0") + "," +
                          (lf.lfItalic ? "1" : "0");
        return Value::make_string(out);
    });

    // FORM.SET(handle, prop$, [value])
    vm.register_native("FORM.SET", 2, 3, [](const std::vector<Value>& args) -> Value {
        int handle = arg_int(args, 0);
        FormsItem& it = item_of(handle, "FORM.SET");
        std::string prop = upname(arg_str(args, 1, "FORM.SET"));
        set_prop(handle, it, prop, args.size() > 2 ? args[2] : Value::make_none(), "FORM.SET");
        return Value::make_none();
    });

    // FORM.GET(handle, prop$, [a], [b]) -> value
    // The extra arguments serve cell addressing: FORM.GET(lv, "CELL", row, col)
    vm.register_native("FORM.GET", 2, 4, [](const std::vector<Value>& args) -> Value {
        int handle = arg_int(args, 0);
        FormsItem& it = item_of(handle, "FORM.GET");
        std::string prop = upname(arg_str(args, 1, "FORM.GET"));
        if (prop == "CELL" && it.kind == "LISTVIEW" && args.size() >= 4)
            return Value::make_string(
                lv_cell_text(it.hwnd, arg_int(args, 2), arg_int(args, 3)));
        return get_prop(handle, it, prop, "FORM.GET");
    });

    // FORM.SHOW(frm)  show a (secondary) form and fire its LOAD event
    vm.register_native("FORM.SHOW", 1, 1, [](const std::vector<Value>& args) -> Value {
        FormsItem& f = form_of(arg_int(args, 0), "FORM.SHOW");
        ShowWindow(f.hwnd, SW_SHOW);
        UpdateWindow(f.hwnd);
        bring_to_front(f.hwnd);
        g_last_form = f.hwnd;
        queue_event(f.name, "LOAD", info_map(f));
        drain_events();
        return Value::make_none();
    });

    // FORM.RUN(frm)  show the form and run the message loop until every
    // form is closed. Events dispatch on this (the VM) thread.
    vm.register_native("FORM.RUN", 1, 1, [](const std::vector<Value>& args) -> Value {
        FormsItem& f = form_of(arg_int(args, 0), "FORM.RUN");
        ShowWindow(f.hwnd, SW_SHOW);
        UpdateWindow(f.hwnd);
        bring_to_front(f.hwnd);
        g_last_form = f.hwnd;
        queue_event(f.name, "LOAD", info_map(f));
        drain_events();

        MSG msg;
        while (g_open_forms > 0) {
            BOOL r = GetMessageW(&msg, nullptr, 0, 0);
            if (r <= 0) break;
            pump_one(msg);
            drain_events();
        }
        return Value::make_none();
    });

    // FORM.DOEVENTS() -> bool  pump pending messages once; TRUE while any
    // form is still open. For cooperative DO ... LOOP style programs.
    vm.register_native("FORM.DOEVENTS", 0, 0, [](const std::vector<Value>&) -> Value {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (!pump_one(msg)) break;
        }
        drain_events();
        return Value::make_bool(g_open_forms > 0);
    });

    // FORM.CLOSE(frm)
    vm.register_native("FORM.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        FormsItem& f = form_of(arg_int(args, 0), "FORM.CLOSE");
        // Through WM_CLOSE, not straight to DestroyWindow: closing from code
        // has to run the same UNLOAD - and be refusable by it - as the window
        // cross does. Before this it fired no UNLOAD at all.
        SendMessageW(f.hwnd, WM_CLOSE, 0, 0);
        return Value::make_none();
    });

    // MSGBOX(text$, [flags], [title$]) -> button code (1=OK 2=Cancel 6=Yes 7=No)
    // Flags follow the Win32/VB6 constants: 1=OKCancel 4=YesNo 16=Critical
    // 32=Question 48=Exclamation 64=Information.
    vm.register_native("MSGBOX", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string text = value_text(args[0]);
        UINT flags = args.size() > 1 ? (UINT)args[1].to_int() : MB_OK;
        std::string title = args.size() > 2 ? value_text(args[2]) : "jdBasic";
        HWND owner = g_last_form && IsWindow(g_last_form) ? g_last_form : nullptr;
        int r = MessageBoxW(owner, widen(text).c_str(), widen(title).c_str(), flags);
        return Value::make_i64(r);
    });

    // INPUTBOX$(prompt$, [title$], [default$]) -> entered text ("" on cancel)
    vm.register_native("INPUTBOX$", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string prompt = value_text(args[0]);
        std::string title  = args.size() > 1 ? value_text(args[1]) : "jdBasic";
        std::string deflt  = args.size() > 2 ? value_text(args[2]) : "";
        return Value::make_string(run_inputbox(prompt, title, deflt));
    });
}

#else // !FORMS || !_WIN32

void register_forms_builtins(VM&) {}

#endif
