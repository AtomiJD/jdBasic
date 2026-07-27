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

#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

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
    std::string kind;           // FORM, BUTTON, LABEL, TEXTBOX, ...
    int         form = 0;       // owning form handle (0 for a form itself)
};

std::map<int, FormsItem>  g_items;
std::map<HWND, int>       g_byhwnd;
int                       g_next_handle = 1;
int                       g_open_forms  = 0;
HFONT                     g_font        = nullptr;
VM*                       g_vm          = nullptr;
HWND                      g_last_form   = nullptr;

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

FormsItem& form_of(int handle, const char* who) {
    FormsItem& f = item_of(handle, who);
    if (f.kind != "FORM")
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

void drain_events() {
    while (!g_evq.empty()) {
        PendingEvent ev = std::move(g_evq.front());
        g_evq.pop_front();
        g_vm->event_raise(ev.name, {ev.info});
    }
}

// ── Window class / WndProc ──────────────────────────────────────

LRESULT CALLBACK form_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            HWND ctl = (HWND)lParam;
            if (!ctl) break;
            auto hit = g_byhwnd.find(ctl);
            if (hit == g_byhwnd.end()) break;
            FormsItem& it = g_items[hit->second];
            int code = HIWORD(wParam);

            if (it.kind == "BUTTON" && code == BN_CLICKED) {
                queue_event(it.name, "CLICK", info_map(it));
            } else if ((it.kind == "CHECKBOX" || it.kind == "RADIO") && code == BN_CLICKED) {
                Value m = info_map(it);
                m.as_object()->set("checked",
                    Value::make_bool(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED));
                queue_event(it.name, "CLICK", m);
            } else if (it.kind == "TEXTBOX" && code == EN_CHANGE) {
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
                m.as_object()->set("width", Value::make_i64(LOWORD(lParam)));
                m.as_object()->set("height", Value::make_i64(HIWORD(lParam)));
                queue_event(it.name, "RESIZE", m);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_CLOSE: {
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end())
                queue_event(g_items[hit->second].name, "UNLOAD", info_map(g_items[hit->second]));
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            auto hit = g_byhwnd.find(hwnd);
            if (hit != g_byhwnd.end()) {
                int form_handle = hit->second;
                for (auto it = g_items.begin(); it != g_items.end();) {
                    if (it->first == form_handle || it->second.form == form_handle) {
                        g_byhwnd.erase(it->second.hwnd);
                        it = g_items.erase(it);
                    } else {
                        ++it;
                    }
                }
                // No PostQuitMessage: FORM.RUN and FORM.DOEVENTS re-check
                // g_open_forms after every dispatch, and a queued WM_QUIT
                // would leak into whichever message loop runs next.
                if (--g_open_forms < 0) g_open_forms = 0;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t* FORM_CLASS = L"JDBForm";

void ensure_class() {
    static bool done = false;
    if (done) return;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = form_wndproc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = FORM_CLASS;
    RegisterClassW(&wc);

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    done = true;
}

// ── Creation helpers ────────────────────────────────────────────

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
                               x, y, w, h, f.hwnd,
                               (HMENU)(INT_PTR)handle,
                               GetModuleHandleW(nullptr), nullptr);
    if (!ctl)
        throw std::runtime_error("FORM." + kind + ": CreateWindow failed for '" + name + "'");
    if (g_font) SendMessageW(ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
    return store_item(ctl, name, kind, frm);
}

int do_create_form(const std::string& title, int cw, int ch, const std::string& name) {
    ensure_class();
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExW(0, FORM_CLASS, widen(title).c_str(),
                                WS_OVERLAPPEDWINDOW,
                                (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw std::runtime_error("FORM.CREATE: CreateWindow failed");
    g_open_forms++;
    g_last_form = hwnd;
    return store_item(hwnd, name, "FORM", 0);
}

int arg_int(const std::vector<Value>& args, size_t i) { return (int)args[i].to_int(); }

std::string arg_str(const std::vector<Value>& args, size_t i, const char* who) {
    if (args[i].type != ValueType::STRING)
        throw std::runtime_error(std::string(who) + ": argument " + std::to_string(i + 1) + " must be a string");
    return args[i].as_string()->data;
}

// ── Message pumping ─────────────────────────────────────────────

// Pump one message, returns FALSE on WM_QUIT. Tab/arrow navigation goes
// through IsDialogMessage against the message's own top-level window.
bool pump_one(MSG& msg) {
    if (msg.message == WM_QUIT) return false;
    HWND root = GetAncestor(msg.hwnd, GA_ROOT);
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
    RECT rc = { 0, 0, W, H };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"JDBInputBox", widen(title).c_str(),
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND lbl  = CreateWindowExW(0, L"STATIC", widen(prompt).c_str(),
                                WS_CHILD | WS_VISIBLE, 12, 12, W - 24, 40,
                                dlg, nullptr, inst, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(deflt).c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                12, 60, W - 24, 24, dlg, nullptr, inst, nullptr);
    HWND okb  = CreateWindowExW(0, L"BUTTON", L"OK",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                W - 180, 100, 80, 26, dlg, (HMENU)IDOK, inst, nullptr);
    HWND cnb  = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                W - 92, 100, 80, 26, dlg, (HMENU)IDCANCEL, inst, nullptr);
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
    bool is_list = it.kind == "LISTBOX" || it.kind == "COMBO";
    HWND h = it.hwnd;

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
        int n = (int)v.to_int();
        if (prop == "X") x = n; else if (prop == "Y") y = n;
        else if (prop == "WIDTH") w = n; else hh = n;
        MoveWindow(h, x, y, w, hh, TRUE);
    } else {
        throw std::runtime_error(std::string(who) + ": unknown property '" + prop +
                                 "' for " + it.kind);
    }
}

Value get_prop(FormsItem& it, const std::string& prop, const char* who) {
    bool is_list = it.kind == "LISTBOX" || it.kind == "COMBO";
    HWND h = it.hwnd;

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
        if (prop == "WIDTH")  return Value::make_i64(rc.right - rc.left);
        if (prop == "HEIGHT") return Value::make_i64(rc.bottom - rc.top);
        POINT tl = { rc.left, rc.top };
        HWND parent = GetParent(h);
        if (parent) ScreenToClient(parent, &tl);
        return Value::make_i64(prop == "X" ? tl.x : tl.y);
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

// Event names a control kind can fire; used for automatic handler binding.
const std::vector<const char*>& events_of(const std::string& kind) {
    static const std::vector<const char*> form_ev  = { "LOAD", "UNLOAD", "RESIZE" };
    static const std::vector<const char*> click_ev = { "CLICK" };
    static const std::vector<const char*> text_ev  = { "CHANGE" };
    static const std::vector<const char*> list_ev  = { "CLICK", "DBLCLICK" };
    static const std::vector<const char*> tick_ev  = { "TICK" };
    static const std::vector<const char*> none_ev  = {};
    if (kind == "FORM")     return form_ev;
    if (kind == "BUTTON" || kind == "CHECKBOX" || kind == "RADIO") return click_ev;
    if (kind == "TEXTBOX" || kind == "COMBO")  return text_ev;
    if (kind == "LISTBOX")  return list_ev;
    if (kind == "TIMER")    return tick_ev;
    return none_ev;
}

void bind_handlers(const std::string& ctl_name, const std::string& kind) {
    for (const char* ev : events_of(kind)) {
        std::string handler = ctl_name + "_" + ev;
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

        bind_handlers(g_items[handle].name, g_items[handle].kind);
    }
}

int load_jdform(const std::string& path) {
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
    int frm = do_create_form(jf_str(*fdef, "title", "form", ""),
                             jf_int(*fdef, "width", "form"),
                             jf_int(*fdef, "height", "form"), fname);
    // A bad control entry must not leave the half-built (still invisible)
    // form behind - it would count as open forever.
    try {
        bind_handlers(fname, "FORM");
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
        if (ms > 0) SetTimer(f.hwnd, handle, ms, nullptr);
        return Value::make_i64(handle);
    });

    // FORM.LOAD(path$) -> handle  instantiate a .jdform file and bind
    // every <NAME>_<EVENT> handler that exists in the program
    vm.register_native("FORM.LOAD", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(load_jdform(arg_str(args, 0, "FORM.LOAD")));
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

    // FORM.SET(handle, prop$, [value])
    vm.register_native("FORM.SET", 2, 3, [](const std::vector<Value>& args) -> Value {
        int handle = arg_int(args, 0);
        FormsItem& it = item_of(handle, "FORM.SET");
        std::string prop = upname(arg_str(args, 1, "FORM.SET"));
        set_prop(handle, it, prop, args.size() > 2 ? args[2] : Value::make_none(), "FORM.SET");
        return Value::make_none();
    });

    // FORM.GET(handle, prop$) -> value
    vm.register_native("FORM.GET", 2, 2, [](const std::vector<Value>& args) -> Value {
        FormsItem& it = item_of(arg_int(args, 0), "FORM.GET");
        return get_prop(it, upname(arg_str(args, 1, "FORM.GET")), "FORM.GET");
    });

    // FORM.SHOW(frm)  show a (secondary) form and fire its LOAD event
    vm.register_native("FORM.SHOW", 1, 1, [](const std::vector<Value>& args) -> Value {
        FormsItem& f = form_of(arg_int(args, 0), "FORM.SHOW");
        ShowWindow(f.hwnd, SW_SHOW);
        UpdateWindow(f.hwnd);
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
        DestroyWindow(f.hwnd);
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
