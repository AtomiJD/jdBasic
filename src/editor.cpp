// token.h MUST come before windows.h to avoid macro conflicts
#include "token.h"
#include "natives_list.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "editor.h"

// Toggled by OPTION "NOAUTOIDENT" / "AUTOIDENT" in vm.cpp. When false,
// pressing Enter inserts a bare newline — the next line starts at col 0
// instead of mirroring the previous line's leading whitespace. Useful
// for keyboard-driven scripts that already supply their own indentation
// (e.g. jdb/tv/director.jdb typing pre-indented code into the editor).
bool g_editor_autoindent = true;

#if !defined(_WIN32)
// ── POSIX full-screen editor ────────────────────────────────
// Termios raw-mode + ANSI-escape full-screen edit. Same Editor class API
// as the Windows version. Keymap chosen to mirror the Win editor where
// the keys are reachable on a Linux terminal:
//   arrows, Home/End, PageUp/Down, Backspace/Delete, Enter
//   Ctrl+S = save     Ctrl+Q = quit (prompts if dirty)
//   F5     = run (compile + execute current buffer)
//   Ctrl+Z / Ctrl+Y = undo / redo
//   Ctrl+C / Ctrl+V = copy line / paste (line-based for simplicity)
//   Ctrl+G = go to line
//   Ctrl+F = find next
// Syntax highlighting reuses keywords() / native_names() from the parser.
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>

namespace {

// Abstract key codes (private to this TU, mirroring the Win editor's VKs)
enum : int {
    PK_NONE = 0,
    PK_LEFT = 1000, PK_RIGHT, PK_UP, PK_DOWN,
    PK_HOME, PK_END, PK_PAGEUP, PK_PAGEDOWN,
    PK_DELETE, PK_BACKSPACE, PK_ENTER, PK_TAB, PK_ESC,
    PK_F5,
    PK_CTRL_S, PK_CTRL_Q, PK_CTRL_Z, PK_CTRL_Y,
    PK_CTRL_C, PK_CTRL_X, PK_CTRL_V, PK_CTRL_G, PK_CTRL_F, PK_CTRL_A,
    PK_CTRL_LEFT, PK_CTRL_RIGHT,
    PK_SHIFT_LEFT, PK_SHIFT_RIGHT, PK_SHIFT_UP, PK_SHIFT_DOWN,
    PK_SHIFT_HOME, PK_SHIFT_END,
};

// Internal clipboard fallback. xclip/wl-copy fail silently when there's no
// DISPLAY/WAYLAND_DISPLAY (e.g. plain SSH session) — without this fallback
// Ctrl+C+V across the editor would round-trip through the system clipboard
// or stale state from another app.
static std::string g_internal_clipboard;
static bool        g_internal_clipboard_set = false;

struct TermGuard {
    struct termios orig{};
    bool active = false;
    void enable() {
        if (active) return;
        if (tcgetattr(STDIN_FILENO, &orig) != 0) return;
        struct termios raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN]  = 1;   // block until at least 1 byte (editor is event-driven)
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        active = true;
    }
    void disable() {
        if (!active) return;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        active = false;
    }
    ~TermGuard() { disable(); }
};

// Read one byte non-blocking (used after ESC, with a short timeout)
int read_byte_timed(int ms) {
    fd_set rfds; FD_ZERO(&rfds); FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) <= 0) return -1;
    unsigned char ch;
    return (::read(STDIN_FILENO, &ch, 1) == 1) ? (int)ch : -1;
}

// Blocking read with full ANSI/CSI escape parsing.
int read_key_blocking(std::string& utf8_out) {
    utf8_out.clear();
    unsigned char c;
    if (::read(STDIN_FILENO, &c, 1) != 1) return PK_NONE;

    if (c == 0x7F || c == 0x08) return PK_BACKSPACE;
    if (c == '\r' || c == '\n') return PK_ENTER;
    if (c == '\t') return PK_TAB;
    if (c == 0x13) return PK_CTRL_S;
    if (c == 0x11) return PK_CTRL_Q;
    if (c == 0x1A) return PK_CTRL_Z;
    if (c == 0x19) return PK_CTRL_Y;
    if (c == 0x03) return PK_CTRL_C;
    if (c == 0x16) return PK_CTRL_V;
    if (c == 0x18) return PK_CTRL_X;
    if (c == 0x07) return PK_CTRL_G;
    if (c == 0x06) return PK_CTRL_F;
    if (c == 0x01) return PK_CTRL_A;

    if (c == 0x1B) {
        int b1 = read_byte_timed(20);
        if (b1 < 0) return PK_ESC;
        if (b1 != '[' && b1 != 'O') return PK_ESC;

        std::string seq; seq += (char)b1;
        for (int i = 0; i < 16; i++) {
            int b = read_byte_timed(20);
            if (b < 0) break;
            seq += (char)b;
            if (b >= 0x40 && b <= 0x7E) break;
        }
        char final = seq.empty() ? 0 : seq.back();

        if (b1 == 'O') {
            switch (final) {
                case 'P': case 'Q': case 'R': case 'S': return PK_NONE;  // F1-F4 unmapped
                case 'H': return PK_HOME;
                case 'F': return PK_END;
            }
            return PK_NONE;
        }

        std::string params = seq.substr(1, seq.size() - 2);
        std::vector<int> nums;
        { int cur = -1; for (char ch : params) {
            if (ch >= '0' && ch <= '9') { if (cur < 0) cur = 0; cur = cur * 10 + (ch - '0'); }
            else if (ch == ';') { nums.push_back(cur); cur = -1; }
        } if (cur >= 0) nums.push_back(cur); }
        int p1 = nums.size() > 0 ? nums[0] : -1;
        int p2 = nums.size() > 1 ? nums[1] : -1;
        // xterm modifier conventions: p2 = 1+(shift?1:0)+(alt?2:0)+(ctrl?4:0)
        bool shift_mod = (p2 == 2 || p2 == 4 || p2 == 6 || p2 == 8);
        bool ctrl_mod  = (p2 == 5 || p2 == 6 || p2 == 7 || p2 == 8);

        switch (final) {
            case 'A': return shift_mod ? PK_SHIFT_UP    : PK_UP;
            case 'B': return shift_mod ? PK_SHIFT_DOWN  : PK_DOWN;
            case 'C': return ctrl_mod ? PK_CTRL_RIGHT : (shift_mod ? PK_SHIFT_RIGHT : PK_RIGHT);
            case 'D': return ctrl_mod ? PK_CTRL_LEFT  : (shift_mod ? PK_SHIFT_LEFT  : PK_LEFT);
            case 'H': return shift_mod ? PK_SHIFT_HOME  : PK_HOME;
            case 'F': return shift_mod ? PK_SHIFT_END   : PK_END;
            case '~':
                switch (p1) {
                    case 1: case 7:  return shift_mod ? PK_SHIFT_HOME : PK_HOME;
                    case 4: case 8:  return shift_mod ? PK_SHIFT_END  : PK_END;
                    case 3:  return PK_DELETE;
                    case 5:  return PK_PAGEUP;
                    case 6:  return PK_PAGEDOWN;
                    case 15: return PK_F5;
                }
                return PK_NONE;
        }
        return PK_NONE;
    }

    // Plain UTF-8 byte: collect continuation bytes if needed
    utf8_out += (char)c;
    int extra = 0;
    if      ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    for (int i = 0; i < extra; i++) {
        unsigned char k;
        if (::read(STDIN_FILENO, &k, 1) != 1) break;
        utf8_out += (char)k;
    }
    return -1;  // signal "literal char in utf8_out"
}

// UTF-8 codepoint count (= visual columns for ASCII; close enough for now)
int utf8_codepoints(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) {
        if (c < 0x80 || c >= 0xC0) n++;
    }
    return n;
}
// Substring measured in codepoints, returning byte offset for `cp` codepoints
int cp_to_byte(const std::string& s, int cp) {
    int seen = 0; size_t i = 0;
    while (i < s.size() && seen < cp) {
        unsigned char c = s[i];
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        seen++;
    }
    return (int)i;
}
int byte_to_cp(const std::string& s, int byte_off) {
    int cp = 0; size_t i = 0;
    while ((int)i < byte_off && i < s.size()) {
        unsigned char c = s[i];
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        cp++;
    }
    return cp;
}

// Base64 helper for OSC 52 clipboard transport.
static std::string base64_encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) | c;
        bits += 8;
        while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}

// Clipboard. Always updates in-process fallback so internal copy/paste works
// even with no DISPLAY. Also pushes to the *terminal's* clipboard via OSC 52
// — most modern terminals (xterm, kitty, alacritty, wezterm, foot,
// gnome-terminal recent) support it. That makes the user's Ctrl+V work
// consistently when their terminal intercepts it (common case on Linux/SSH).
// Falls back to xclip/wl-copy/xsel when X/Wayland is around for the local
// case.
void clip_set(const std::string& text) {
    g_internal_clipboard = text;
    g_internal_clipboard_set = true;
    // OSC 52 set: ESC ] 52 ; c ; <base64> BEL
    std::string osc = "\033]52;c;" + base64_encode(text) + "\007";
    write(STDOUT_FILENO, osc.data(), osc.size());
    const char* tools[] = {
        "wl-copy 2>/dev/null",
        "xclip -selection clipboard -in 2>/dev/null",
        "xsel --clipboard --input 2>/dev/null", nullptr };
    for (int i = 0; tools[i]; ++i) {
        FILE* p = popen(tools[i], "w"); if (!p) continue;
        fwrite(text.data(), 1, text.size(), p);
        if (pclose(p) == 0) return;
    }
}
std::string clip_get() {
    // If the user copied something inside this editor session, prefer that —
    // the system clipboard might be unreachable (no DISPLAY) or out of sync.
    if (g_internal_clipboard_set) return g_internal_clipboard;
    std::string r;
    const char* tools[] = {
        "wl-paste --no-newline 2>/dev/null",
        "xclip -selection clipboard -out 2>/dev/null",
        "xsel --clipboard --output 2>/dev/null", nullptr };
    for (int i = 0; tools[i]; ++i) {
        FILE* p = popen(tools[i], "r"); if (!p) continue;
        char buf[1024]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) r.append(buf, n);
        if (pclose(p) == 0 && !r.empty()) return r;
        r.clear();
    }
    return r;
}

class EditorImpl {
public:
    EditorImpl(std::vector<std::string>& lines, const std::string& fname, bool* run_flag)
        : lines_ref(lines), filename(fname), run_requested_out(run_flag)
    {
        if (lines_ref.empty()) lines_ref.push_back("");
        update_screen_size();
    }
    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    bool* run_requested_out;
    int screen_cols = 80, screen_rows = 24;
    int text_rows() const { return std::max(1, screen_rows - 2); }  // status + msg
    int cy = 0, cx = 0;     // cursor in (line, codepoint-column)
    int top_row = 0, left_col = 0;
    bool dirty = false;
    std::string status_msg;

    // Selection state. has_sel() is true between Shift-arrow press and any
    // non-shift movement (or selection-ending action like type/backspace).
    bool sel_active = false;
    int  sel_anchor_y = 0, sel_anchor_x = 0;   // anchor (set on first shift+move)
    bool has_sel() const { return sel_active && !(sel_anchor_y == cy && sel_anchor_x == cx); }
    void start_sel_if_needed() {
        if (!sel_active) { sel_active = true; sel_anchor_y = cy; sel_anchor_x = cx; }
    }
    void clear_sel() { sel_active = false; }
    // Normalised range (sy,sx) <= (ey,ex)
    void get_sel_range(int& sy, int& sx, int& ey, int& ex) const {
        if (sel_anchor_y < cy || (sel_anchor_y == cy && sel_anchor_x <= cx)) {
            sy = sel_anchor_y; sx = sel_anchor_x; ey = cy; ex = cx;
        } else {
            sy = cy; sx = cx; ey = sel_anchor_y; ex = sel_anchor_x;
        }
    }
    std::string sel_text() const {
        if (!has_sel()) return "";
        int sy, sx, ey, ex; get_sel_range(sy, sx, ey, ex);
        std::string out;
        for (int y = sy; y <= ey; y++) {
            const std::string& l = lines_ref[y];
            int from_byte = (y == sy) ? cp_to_byte(l, sx) : 0;
            int to_byte   = (y == ey) ? cp_to_byte(l, ex) : (int)l.size();
            out.append(l, from_byte, to_byte - from_byte);
            if (y != ey) out += '\n';
        }
        return out;
    }
    void delete_sel() {
        if (!has_sel()) return;
        int sy, sx, ey, ex; get_sel_range(sy, sx, ey, ex);
        if (sy == ey) {
            int from = cp_to_byte(lines_ref[sy], sx);
            int to   = cp_to_byte(lines_ref[sy], ex);
            lines_ref[sy].erase(from, to - from);
        } else {
            int from = cp_to_byte(lines_ref[sy], sx);
            int to   = cp_to_byte(lines_ref[ey], ex);
            std::string head = lines_ref[sy].substr(0, from);
            std::string tail = lines_ref[ey].substr(to);
            lines_ref[sy] = head + tail;
            lines_ref.erase(lines_ref.begin() + sy + 1, lines_ref.begin() + ey + 1);
        }
        cy = sy; cx = sx;
        clear_sel();
        dirty = true;
    }

    // Undo/Redo
    struct Snap { std::vector<std::string> lines; int cx, cy; };
    std::vector<Snap> undo_stack, redo_stack;
    void save_state() {
        undo_stack.push_back({lines_ref, cx, cy});
        if (undo_stack.size() > 200) undo_stack.erase(undo_stack.begin());
        redo_stack.clear();
    }
    void undo() {
        if (undo_stack.empty()) { status_msg = "Nothing to undo"; return; }
        redo_stack.push_back({lines_ref, cx, cy});
        auto& s = undo_stack.back();
        lines_ref = s.lines; cx = s.cx; cy = s.cy;
        undo_stack.pop_back();
        dirty = true;
    }
    void redo() {
        if (redo_stack.empty()) { status_msg = "Nothing to redo"; return; }
        undo_stack.push_back({lines_ref, cx, cy});
        auto& s = redo_stack.back();
        lines_ref = s.lines; cx = s.cx; cy = s.cy;
        redo_stack.pop_back();
        dirty = true;
    }

    void update_screen_size() {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            screen_cols = ws.ws_col;
            screen_rows = ws.ws_row;
        }
    }

    // Cursor / scroll bookkeeping
    int line_cps(int idx) const {
        if (idx < 0 || idx >= (int)lines_ref.size()) return 0;
        return utf8_codepoints(lines_ref[idx]);
    }
    void clamp_cursor() {
        if (cy < 0) cy = 0;
        if (cy >= (int)lines_ref.size()) cy = (int)lines_ref.size() - 1;
        int len = line_cps(cy);
        if (cx > len) cx = len;
        if (cx < 0) cx = 0;
    }
    void scroll_to_cursor() {
        int rows = text_rows();
        if (cy < top_row) top_row = cy;
        if (cy >= top_row + rows) top_row = cy - rows + 1;
        if (cx < left_col) left_col = cx;
        if (cx >= left_col + screen_cols) left_col = cx - screen_cols + 1;
    }

    void draw();
    void draw_status();
    void draw_line(int line_idx, int row_on_screen);

    void save_file() {
        std::ofstream out(filename);
        if (!out) { status_msg = "Save failed: " + filename; return; }
        for (size_t i = 0; i < lines_ref.size(); i++) {
            out << lines_ref[i];
            if (i + 1 < lines_ref.size()) out << "\n";
        }
        dirty = false;
        status_msg = "Saved: " + filename;
    }
    bool prompt_yn(const std::string& q) {
        std::cout << "\033[" << screen_rows << ";1H\033[K" << q << " (y/n) " << std::flush;
        std::string utf;
        while (true) {
            int k = read_key_blocking(utf);
            if (k == -1 && !utf.empty()) {
                char c = utf[0];
                if (c == 'y' || c == 'Y') return true;
                if (c == 'n' || c == 'N') return false;
            }
            if (k == PK_ESC) return false;
        }
    }
    std::string prompt_str(const std::string& q) {
        std::string buf;
        while (true) {
            std::cout << "\033[" << screen_rows << ";1H\033[K"
                      << q << buf << "\033[?25h" << std::flush;
            std::string utf;
            int k = read_key_blocking(utf);
            if (k == PK_ENTER) return buf;
            if (k == PK_ESC)   return "";
            if (k == PK_BACKSPACE) {
                if (!buf.empty()) {
                    while (!buf.empty() && (((unsigned char)buf.back() & 0xC0) == 0x80))
                        buf.pop_back();
                    if (!buf.empty()) buf.pop_back();
                }
            } else if (k == -1 && !utf.empty()) {
                buf += utf;
            }
        }
    }
    void go_to_line() {
        std::string s = prompt_str("Goto line: ");
        if (s.empty()) return;
        try { int n = std::stoi(s); if (n < 1) n = 1;
              cy = std::min((int)lines_ref.size() - 1, n - 1); cx = 0;
              status_msg = "Jumped to line " + std::to_string(n);
        } catch (...) { status_msg = "Bad line number"; }
    }
    void find_next() {
        static std::string last_query;
        std::string q = prompt_str("Find: ");
        if (!q.empty()) last_query = q;
        if (last_query.empty()) return;
        for (int dy = 0; dy <= (int)lines_ref.size(); dy++) {
            int idx = (cy + dy) % lines_ref.size();
            int from = (dy == 0) ? cp_to_byte(lines_ref[idx], cx + 1) : 0;
            size_t pos = lines_ref[idx].find(last_query, from);
            if (pos != std::string::npos) {
                cy = idx; cx = byte_to_cp(lines_ref[idx], (int)pos);
                status_msg = "Match line " + std::to_string(cy + 1);
                return;
            }
        }
        status_msg = "Not found: " + last_query;
    }

    bool is_kw(const std::string& w) const {
        std::string u = w; std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        return keywords().count(u) > 0;
    }
    bool is_nat(const std::string& w) const {
        std::string u = w; std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        return native_names().count(u) > 0;
    }
};

void EditorImpl::draw_line(int line_idx, int row_on_screen) {
    std::cout << "\033[" << (row_on_screen + 1) << ";1H\033[K";
    if (line_idx >= (int)lines_ref.size()) {
        std::cout << "\033[34m~\033[0m";
        return;
    }
    const std::string& line = lines_ref[line_idx];
    int line_cp_total = utf8_codepoints(line);

    // Compute selection columns for this row (in codepoints)
    int sel_from = -1, sel_to = -1;
    if (has_sel()) {
        int sy, sx, ey, ex; get_sel_range(sy, sx, ey, ex);
        if (line_idx >= sy && line_idx <= ey) {
            sel_from = (line_idx == sy) ? sx : 0;
            sel_to   = (line_idx == ey) ? ex : line_cp_total + 1;  // +1 = include newline cell
        }
    }

    if (left_col >= line_cp_total && sel_from < 0) return;

    int skip_byte = cp_to_byte(line, left_col);
    int max_cp = screen_cols;
    int cp_emitted = 0;
    int cp_pos = left_col;   // codepoint index of next emitted cell

    auto open_color = [&](const char* color) {
        bool selected = (sel_from >= 0 && cp_pos >= sel_from && cp_pos < sel_to);
        if (selected) std::cout << "\033[7m";   // reverse video
        std::cout << color;
    };
    auto close_color = [&]() {
        std::cout << "\033[0m";
    };

    size_t i = (size_t)skip_byte;
    while (i < line.size() && cp_emitted < max_cp) {
        unsigned char c = (unsigned char)line[i];
        std::string token;
        const char* color = "\033[0m";

        if (c == '\'') {
            color = "\033[90m";
            while (i < line.size()) { token += line[i++]; }
        } else if (c == '"') {
            color = "\033[36m";
            token += line[i++];
            while (i < line.size() && line[i] != '"') token += line[i++];
            if (i < line.size()) token += line[i++];
        } else if (std::isdigit(c)) {
            color = "\033[33m";
            while (i < line.size() && (std::isdigit((unsigned char)line[i]) || line[i] == '.'))
                token += line[i++];
        } else if (std::isalpha(c) || c == '_' || c >= 0x80) {
            while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_' ||
                   (unsigned char)line[i] >= 0x80))
                token += line[i++];
            if      (is_kw(token))  color = "\033[35;1m";
            else if (is_nat(token)) color = "\033[36;1m";
            else                    color = "\033[32m";
        } else {
            color = "\033[37m";
            token += line[i++];
        }
        int tk_cp = utf8_codepoints(token);
        if (cp_emitted + tk_cp > max_cp) {
            int allowed = max_cp - cp_emitted;
            int byte_lim = cp_to_byte(token, allowed);
            // Emit char-by-char so selection boundaries inside the truncated
            // token render correctly.
            size_t bi = 0;
            while ((int)bi < byte_lim) {
                unsigned char ch = token[bi];
                int clen = (ch < 0x80) ? 1 : ((ch & 0xE0) == 0xC0 ? 2 : ((ch & 0xF0) == 0xE0 ? 3 : 4));
                if ((int)bi + clen > byte_lim) break;
                open_color(color);
                std::cout << token.substr(bi, clen);
                close_color();
                bi += clen;
                cp_pos++; cp_emitted++;
            }
            cp_emitted = max_cp;
        } else {
            // Emit char-by-char if any selection boundary falls inside the token
            if (sel_from >= 0 && cp_pos < sel_to && cp_pos + tk_cp > sel_from) {
                size_t bi = 0;
                while (bi < token.size()) {
                    unsigned char ch = token[bi];
                    int clen = (ch < 0x80) ? 1 : ((ch & 0xE0) == 0xC0 ? 2 : ((ch & 0xF0) == 0xE0 ? 3 : 4));
                    open_color(color);
                    std::cout << token.substr(bi, clen);
                    close_color();
                    bi += clen;
                    cp_pos++; cp_emitted++;
                }
            } else {
                std::cout << color << token << "\033[0m";
                cp_emitted += tk_cp;
                cp_pos += tk_cp;
            }
        }
    }

    // If the selection extends past the visible text on this row (multi-line
    // selection through whitespace), draw a single reverse-video space at end.
    if (sel_from >= 0 && cp_pos < sel_to && cp_emitted < max_cp) {
        std::cout << "\033[7m \033[0m";
    }
}

void EditorImpl::draw_status() {
    int row = screen_rows - 1;
    std::cout << "\033[" << row << ";1H\033[K\033[7m";  // reverse video
    std::ostringstream s;
    s << " " << (filename.empty() ? "[no name]" : filename)
      << (dirty ? " * " : "   ")
      << "L" << (cy + 1) << "/" << lines_ref.size()
      << " C" << (cx + 1)
      << "  ^S save  ^Q quit  F5 run  ^Z undo  ^G goto  ^F find";
    std::string line = s.str();
    int cp = utf8_codepoints(line);
    if (cp > screen_cols) line = line.substr(0, cp_to_byte(line, screen_cols));
    else                  line.append(screen_cols - cp, ' ');
    std::cout << line << "\033[0m";

    // Message line
    std::cout << "\033[" << screen_rows << ";1H\033[K" << status_msg;
    status_msg.clear();
}

void EditorImpl::draw() {
    update_screen_size();
    scroll_to_cursor();
    std::cout << "\033[?25l";   // hide cursor while redrawing
    int rows = text_rows();
    for (int r = 0; r < rows; r++) {
        draw_line(top_row + r, r);
    }
    draw_status();
    // Position cursor at (cy - top_row, cx - left_col)
    int row = cy - top_row + 1;
    int col = cx - left_col + 1;
    std::cout << "\033[" << row << ";" << col << "H\033[?25h" << std::flush;
}

void EditorImpl::run() {
    TermGuard guard;
    guard.enable();
    std::cout << "\033[?1049h\033[2J\033[H" << std::flush;  // alt screen + clear

    while (true) {
        draw();
        std::string utf;
        int key = read_key_blocking(utf);
        if (key == PK_NONE) continue;

        // Movement keys clear selection on non-shift; the shift variants
        // start it. Anything not in this list (typing, save, etc.) just
        // proceeds — typing replaces the selection.
        bool is_plain_move = (key == PK_LEFT || key == PK_RIGHT || key == PK_UP || key == PK_DOWN ||
                              key == PK_HOME || key == PK_END || key == PK_PAGEUP || key == PK_PAGEDOWN ||
                              key == PK_CTRL_LEFT || key == PK_CTRL_RIGHT);
        if (is_plain_move) clear_sel();

        switch (key) {
            case PK_CTRL_Q:
                // Exit unconditionally. The buffer (lines_ref) is the live
                // in-memory program — main.cpp picks it up after we return,
                // so a "discard?" prompt would be misleading. Ctrl+S still
                // exists for writing to disk.
                goto done;
            case PK_F5:
                if (run_requested_out) *run_requested_out = true;
                goto done;
            case PK_CTRL_S: save_file(); break;
            case PK_CTRL_Z: undo(); break;
            case PK_CTRL_Y: redo(); break;
            case PK_CTRL_G: go_to_line(); break;
            case PK_CTRL_F: find_next(); break;
            case PK_CTRL_A:
                // Select all
                sel_active = true; sel_anchor_y = 0; sel_anchor_x = 0;
                cy = (int)lines_ref.size() - 1;
                cx = line_cps(cy);
                break;
            case PK_CTRL_C:
                if (has_sel()) { clip_set(sel_text()); status_msg = "Selection copied"; }
                else           { clip_set(lines_ref[cy] + "\n"); status_msg = "Line copied"; }
                break;
            case PK_CTRL_X:
                if (has_sel()) { save_state(); clip_set(sel_text()); delete_sel(); status_msg = "Selection cut"; }
                else           { save_state(); clip_set(lines_ref[cy] + "\n");
                                 lines_ref.erase(lines_ref.begin() + cy);
                                 if (lines_ref.empty()) lines_ref.push_back("");
                                 if (cy >= (int)lines_ref.size()) cy = (int)lines_ref.size() - 1;
                                 clamp_cursor(); dirty = true; status_msg = "Line cut"; }
                break;
            case PK_CTRL_V: {
                save_state();
                if (has_sel()) delete_sel();
                std::string txt = clip_get();
                // Insert at cursor; respect newlines by splitting into lines.
                std::vector<std::string> parts; std::string cur;
                for (char c : txt) { if (c == '\n') { parts.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
                parts.push_back(cur);
                if (parts.empty()) break;
                int byte_at = cp_to_byte(lines_ref[cy], cx);
                std::string left  = lines_ref[cy].substr(0, byte_at);
                std::string right = lines_ref[cy].substr(byte_at);
                if (parts.size() == 1) {
                    lines_ref[cy] = left + parts[0] + right;
                    cx += utf8_codepoints(parts[0]);
                } else {
                    lines_ref[cy] = left + parts[0];
                    for (size_t k = 1; k < parts.size() - 1; k++)
                        lines_ref.insert(lines_ref.begin() + cy + k, parts[k]);
                    lines_ref.insert(lines_ref.begin() + cy + parts.size() - 1, parts.back() + right);
                    cy += (int)parts.size() - 1;
                    cx = utf8_codepoints(parts.back());
                }
                dirty = true;
                break;
            }
            case PK_LEFT:
            case PK_SHIFT_LEFT:
                if (key == PK_SHIFT_LEFT) start_sel_if_needed();
                if (cx > 0) cx--;
                else if (cy > 0) { cy--; cx = line_cps(cy); }
                break;
            case PK_RIGHT:
            case PK_SHIFT_RIGHT: {
                if (key == PK_SHIFT_RIGHT) start_sel_if_needed();
                int len = line_cps(cy);
                if (cx < len) cx++;
                else if (cy + 1 < (int)lines_ref.size()) { cy++; cx = 0; }
                break;
            }
            case PK_UP:
            case PK_SHIFT_UP:
                if (key == PK_SHIFT_UP) start_sel_if_needed();
                if (cy > 0) cy--; clamp_cursor(); break;
            case PK_DOWN:
            case PK_SHIFT_DOWN:
                if (key == PK_SHIFT_DOWN) start_sel_if_needed();
                if (cy + 1 < (int)lines_ref.size()) cy++; clamp_cursor(); break;
            case PK_HOME:        cx = 0; break;
            case PK_SHIFT_HOME:  start_sel_if_needed(); cx = 0; break;
            case PK_END:         cx = line_cps(cy); break;
            case PK_SHIFT_END:   start_sel_if_needed(); cx = line_cps(cy); break;
            case PK_PAGEUP:   cy = std::max(0, cy - text_rows()); top_row = std::max(0, top_row - text_rows()); clamp_cursor(); break;
            case PK_PAGEDOWN: cy = std::min((int)lines_ref.size() - 1, cy + text_rows()); top_row = std::min(std::max(0,(int)lines_ref.size() - text_rows()), top_row + text_rows()); clamp_cursor(); break;
            case PK_CTRL_LEFT: {
                const std::string& l = lines_ref[cy];
                int b = cp_to_byte(l, cx);
                while (b > 0 && std::isspace((unsigned char)l[b-1])) b--;
                while (b > 0 && !std::isspace((unsigned char)l[b-1])) b--;
                cx = byte_to_cp(l, b);
                break;
            }
            case PK_CTRL_RIGHT: {
                const std::string& l = lines_ref[cy];
                int b = cp_to_byte(l, cx);
                int n = (int)l.size();
                while (b < n && !std::isspace((unsigned char)l[b])) b++;
                while (b < n &&  std::isspace((unsigned char)l[b])) b++;
                cx = byte_to_cp(l, b);
                break;
            }
            case PK_BACKSPACE: {
                save_state();
                if (has_sel()) { delete_sel(); break; }
                if (cx > 0) {
                    int b_end   = cp_to_byte(lines_ref[cy], cx);
                    int b_start = cp_to_byte(lines_ref[cy], cx - 1);
                    lines_ref[cy].erase(b_start, b_end - b_start);
                    cx--;
                    dirty = true;
                } else if (cy > 0) {
                    int prev_len = line_cps(cy - 1);
                    lines_ref[cy - 1] += lines_ref[cy];
                    lines_ref.erase(lines_ref.begin() + cy);
                    cy--; cx = prev_len; dirty = true;
                }
                break;
            }
            case PK_DELETE: {
                save_state();
                if (has_sel()) { delete_sel(); break; }
                int len = line_cps(cy);
                if (cx < len) {
                    int b_start = cp_to_byte(lines_ref[cy], cx);
                    int b_end   = cp_to_byte(lines_ref[cy], cx + 1);
                    lines_ref[cy].erase(b_start, b_end - b_start);
                    dirty = true;
                } else if (cy + 1 < (int)lines_ref.size()) {
                    lines_ref[cy] += lines_ref[cy + 1];
                    lines_ref.erase(lines_ref.begin() + cy + 1);
                    dirty = true;
                }
                break;
            }
            case PK_ENTER: {
                save_state();
                if (has_sel()) delete_sel();
                int b = cp_to_byte(lines_ref[cy], cx);
                std::string remain = lines_ref[cy].substr(b);
                lines_ref[cy] = lines_ref[cy].substr(0, b);
                std::string indent;
                if (g_editor_autoindent) {
                    for (char c : lines_ref[cy]) { if (c == ' ' || c == '\t') indent += c; else break; }
                }
                lines_ref.insert(lines_ref.begin() + cy + 1, indent + remain);
                cy++; cx = utf8_codepoints(indent);
                dirty = true;
                break;
            }
            case PK_TAB: {
                save_state();
                if (has_sel()) delete_sel();
                int b = cp_to_byte(lines_ref[cy], cx);
                lines_ref[cy].insert(b, "    ");
                cx += 4; dirty = true;
                break;
            }
            case -1: {
                if (!utf.empty()) {
                    save_state();
                    if (has_sel()) delete_sel();
                    int b = cp_to_byte(lines_ref[cy], cx);
                    lines_ref[cy].insert(b, utf);
                    cx++;
                    dirty = true;
                }
                break;
            }
            default: break;
        }
    }
done:
    // Reset SGR + show cursor + leave alt screen, in that order. The alt-
    // screen restore brings back the prior buffer; the SGR/cursor resets
    // make sure attributes from the editor don't leak into the REPL.
    std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;
    guard.disable();
}

}  // namespace

Editor::Editor(std::vector<std::string>& lines, const std::string& fname)
    : lines_ref(lines), filename(fname) {}

void Editor::run() {
    run_requested = false;
    EditorImpl impl(lines_ref, filename, &run_requested);
    impl.run();
}

#else
// ── Windows full implementation ──────────────────────────────
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>

class EditorImpl {
public:
    EditorImpl(std::vector<std::string>& lines, const std::string& fname,
               bool* run_flag = nullptr)
        : lines_ref(lines), filename(fname), run_requested_out(run_flag) {
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(hOut, &csbi);
        screen_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top;
        screen_rows -= 2; // leave space for status bar
        if (lines_ref.empty()) lines_ref.push_back("");
    }

    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    bool* run_requested_out = nullptr;
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int screen_cols, screen_rows;
    int cx = 0, cy = 0, top_row = 0, left_col = 0;
    int visual_cx = 0;
    bool file_modified = false;
    bool overwrite_mode = false;

    // Selection
    bool is_selecting = false;
    int sel_cx = 0, sel_cy = 0;

    // Undo/Redo
    struct EditorState {
        std::vector<std::string> lines;
        int cx, cy;
    };
    std::vector<EditorState> undo_stack;
    std::vector<EditorState> redo_stack;

    // String conversion helpers
    std::wstring to_wide(const std::string& str) {
        if (str.empty()) return L"";
        int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring w(sz, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], sz);
        w.pop_back();
        return w;
    }

    std::string to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string s(sz, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], sz, NULL, NULL);
        s.pop_back();
        return s;
    }

    int calc_visual_x(int line_idx, int char_pos) {
        if (line_idx >= (int)lines_ref.size()) return 0;
        std::wstring wl = to_wide(lines_ref[line_idx]);
        int vp = 0;
        for (int i = 0; i < char_pos && i < (int)wl.length(); ++i) {
            if (wl[i] == L'\t') vp += 4 - (vp % 4);
            else vp++;
        }
        return vp;
    }

    // Drawing
    void draw_screen();
    void draw_line(const std::string& line, int row, int line_index);
    void clear_screen();
    void set_cursor(int row, int col);
    void write_status(const std::wstring& msg);

    // Navigation
    void move_cursor(int dx, int dy);

    // Editing
    void save_file();
    void find_text();
    void go_to_line();
    void paste_from_clipboard();

    // Undo/Redo
    void save_state();
    void undo();
    void redo();

    // Selection
    bool has_selection() const;
    void get_selection(int& sx, int& sy, int& ex, int& ey) const;
    void delete_selection();
    void copy_to_clipboard();

    // Syntax highlighting: is keyword?
    bool is_kw(const std::string& word) const {
        std::string upper = word;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto& kw = keywords();
        return kw.find(upper) != kw.end();
    }

    // Syntax highlighting: is a known native function (or a part of a dotted
    // native name like "AI" or "LOAD_LLM" for "AI.LOAD_LLM")?
    bool is_native(const std::string& word) const {
        std::string upper = word;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto& ns = native_names();
        return ns.find(upper) != ns.end();
    }

    std::wstring prompt(const std::wstring& msg);
};

// ── Main editor loop ─────────────────────────────────────────

void EditorImpl::run() {
    DWORD original_mode;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &original_mode);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    clear_screen();
    draw_screen();
    visual_cx = calc_visual_x(cy, cx);
    set_cursor(cy - top_row, visual_cx - left_col);

    while (true) {
        INPUT_RECORD input;
        DWORD count;
        ReadConsoleInput(hIn, &input, 1, &count);

        if (input.EventType != KEY_EVENT || !input.Event.KeyEvent.bKeyDown) continue;
        auto key = input.Event.KeyEvent;

        const bool shift = key.dwControlKeyState & SHIFT_PRESSED;
        const bool ctrl = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
        const bool alt = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);

        bool is_move = (key.wVirtualKeyCode == VK_LEFT || key.wVirtualKeyCode == VK_RIGHT ||
            key.wVirtualKeyCode == VK_UP || key.wVirtualKeyCode == VK_DOWN ||
            key.wVirtualKeyCode == VK_HOME || key.wVirtualKeyCode == VK_END ||
            key.wVirtualKeyCode == VK_PRIOR || key.wVirtualKeyCode == VK_NEXT);

        if (is_move) {
            if (shift) { if (!is_selecting) { is_selecting = true; sel_cx = cx; sel_cy = cy; } }
            else is_selecting = false;
        }

        // Ctrl+Shift combos (but NOT AltGr which sends Ctrl+Alt)
        if (ctrl && shift && !alt) {
            switch (key.wVirtualKeyCode) {
            case 'C': copy_to_clipboard(); break;
            case 'X': copy_to_clipboard(); delete_selection(); break;
            case 'V': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            }
        }
        // Ctrl combos (but NOT AltGr)
        else if (ctrl && !alt) {
            switch (key.wVirtualKeyCode) {
            case 'Q': goto exit_editor;
            case 'S': save_file(); break;
            case 'F': find_text(); break;
            case 'G': go_to_line(); break;
            case 'C': copy_to_clipboard(); break;
            case 'X': copy_to_clipboard(); delete_selection(); break;
            case 'P': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            case 'V': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            case 'Z': undo(); break;
            case 'Y': redo(); break;
            case 37: { // Ctrl+Left
                std::wstring wl = to_wide(lines_ref[cy]);
                while (cx > 0 && iswspace(wl[cx - 1])) move_cursor(-1, 0);
                while (cx > 0 && !iswspace(wl[cx - 1])) move_cursor(-1, 0);
                break;
            }
            case 39: { // Ctrl+Right
                std::wstring wl = to_wide(lines_ref[cy]);
                int len = (int)wl.length();
                while (cx < len && !iswspace(wl[cx])) move_cursor(1, 0);
                while (cx < len && iswspace(wl[cx])) move_cursor(1, 0);
                break;
            }
            }
        }
        // Normal keys
        else {
            switch (key.wVirtualKeyCode) {
            case VK_F5:
                // Compile + run the current buffer. Do NOT save: F5 just
                // signals the host (main.cpp) to execute the in-memory text
                // on the active VM. The buffer changes already live in
                // lines_ref, which the host pushes into program_buffer when
                // the editor exits.
                if (run_requested_out) *run_requested_out = true;
                goto exit_editor;
            case VK_LEFT:   move_cursor(-1, 0); break;
            case VK_RIGHT:  move_cursor(1, 0); break;
            case VK_UP:     move_cursor(0, -1); break;
            case VK_DOWN:   move_cursor(0, 1); break;
            case VK_HOME:   cx = 0; break;
            case VK_END:    cx = (int)to_wide(lines_ref[cy]).length(); break;
            case VK_PRIOR:  // PageUp
                cy = std::max(0, cy - screen_rows);
                top_row = std::max(0, top_row - screen_rows);
                break;
            case VK_NEXT:   // PageDown
                cy = std::min((int)lines_ref.size() - 1, cy + screen_rows);
                top_row = std::min((int)lines_ref.size() - screen_rows, top_row + screen_rows);
                if (top_row < 0) top_row = 0;
                break;
            case VK_INSERT: overwrite_mode = !overwrite_mode; break;
            case VK_DELETE: {
                save_state();
                if (has_selection()) { delete_selection(); }
                else {
                    std::wstring wl = to_wide(lines_ref[cy]);
                    if (cx < (int)wl.length()) {
                        wl.erase(cx, 1);
                        lines_ref[cy] = to_utf8(wl);
                        file_modified = true;
                    } else if (cy < (int)lines_ref.size() - 1) {
                        lines_ref[cy] += lines_ref[cy + 1];
                        lines_ref.erase(lines_ref.begin() + cy + 1);
                        file_modified = true;
                    }
                }
                break;
            }
            case VK_RETURN: {
                if (has_selection()) delete_selection();
                save_state();
                std::wstring wl = to_wide(lines_ref[cy]);
                std::wstring remain = wl.substr(cx);
                std::wstring indent;
                if (g_editor_autoindent) {
                    for (wchar_t c : wl) {
                        if (c == L' ' || c == L'\t') indent += c;
                        else break;
                    }
                }
                wl = wl.substr(0, cx);
                lines_ref[cy] = to_utf8(wl);
                lines_ref.insert(lines_ref.begin() + cy + 1, to_utf8(indent + remain));
                cy++; cx = (int)indent.length();
                file_modified = true;
                break;
            }
            case VK_BACK: {
                save_state();
                if (has_selection()) { delete_selection(); }
                else if (cx > 0) {
                    std::wstring wl = to_wide(lines_ref[cy]);
                    wl.erase(cx - 1, 1);
                    lines_ref[cy] = to_utf8(wl);
                    cx--; file_modified = true;
                } else if (cy > 0) {
                    std::wstring prev = to_wide(lines_ref[cy - 1]);
                    cx = (int)prev.length();
                    prev += to_wide(lines_ref[cy]);
                    lines_ref[cy - 1] = to_utf8(prev);
                    lines_ref.erase(lines_ref.begin() + cy);
                    cy--; file_modified = true;
                }
                break;
            }
            case VK_TAB: {
                if (has_selection()) delete_selection();
                save_state();
                std::wstring wl = to_wide(lines_ref[cy]);
                wl.insert(cx, 1, L'\t');
                lines_ref[cy] = to_utf8(wl);
                cx++; file_modified = true;
                break;
            }
            default: {
                wchar_t ch = key.uChar.UnicodeChar;
                if (iswprint(ch)) {
                    if (has_selection()) delete_selection();
                    save_state();
                    std::wstring wl = to_wide(lines_ref[cy]);
                    if (overwrite_mode && cx < (int)wl.length())
                        wl[cx] = ch;
                    else
                        wl.insert(cx, 1, ch);
                    lines_ref[cy] = to_utf8(wl);
                    cx++; file_modified = true;
                }
            }
            }
        }

        // Clamp cursor
        std::wstring cwl = to_wide(lines_ref[cy]);
        if (cx > (int)cwl.length()) cx = (int)cwl.length();

        visual_cx = calc_visual_x(cy, cx);
        if (cy < top_row) top_row = cy;
        if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
        if (visual_cx < left_col) left_col = visual_cx;
        if (visual_cx >= left_col + screen_cols) left_col = visual_cx - screen_cols + 1;

        draw_screen();
        set_cursor(cy - top_row, visual_cx - left_col);
    }
exit_editor:
    SetConsoleMode(hIn, original_mode);
    clear_screen();
}

// ── Drawing ──────────────────────────────────────────────────

void EditorImpl::clear_screen() {
    COORD topLeft = { 0, 0 };
    DWORD written;
    FillConsoleOutputCharacter(hOut, ' ', screen_cols * (screen_rows + 2), topLeft, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, screen_cols * (screen_rows + 2), topLeft, &written);
}

void EditorImpl::set_cursor(int row, int col) {
    COORD pos = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(hOut, pos);
}

void EditorImpl::write_status(const std::wstring& msg) {
    set_cursor(screen_rows, 0);
    DWORD written;
    std::wstring pad = msg;
    if ((int)pad.size() < screen_cols) pad += std::wstring(screen_cols - pad.size(), L' ');
    WriteConsoleW(hOut, pad.c_str(), (DWORD)pad.length(), &written, nullptr);
    set_cursor(screen_rows + 1, 0);
    std::wstring blank(screen_cols, L' ');
    WriteConsoleW(hOut, blank.c_str(), (DWORD)blank.length(), &written, nullptr);
}

void EditorImpl::draw_line(const std::string& line, int row, int line_index) {
    std::wstring wl = to_wide(line);
    int max_vis = left_col + screen_cols;
    std::vector<CHAR_INFO> buf(max_vis);

    for (auto& c : buf) {
        c.Char.UnicodeChar = L' ';
        c.Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    COORD bufSz = { (SHORT)screen_cols, 1 };
    COORD bufCo = { 0, 0 };
    SMALL_RECT wr = { 0, (SHORT)row, (SHORT)(screen_cols - 1), (SHORT)row };

    int sx = 0, sy = 0, ex = 0, ey = 0;
    bool sel = has_selection();
    if (sel) get_selection(sx, sy, ex, ey);

    auto apply_sel = [&](size_t ci, WORD& attr) {
        if (!sel) return;
        if (line_index > sy && line_index < ey)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == sy && line_index == ey && (int)ci >= sx && (int)ci < ex)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == sy && line_index != ey && (int)ci >= sx)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == ey && line_index != sy && (int)ci < ex)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    };

    const WORD BASE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

    int col = 0;
    size_t i = 0;
    while (i < wl.size() && col < max_vis) {
        // Tab
        if (wl[i] == L'\t') {
            int sp = 4 - (col % 4);
            for (int s = 0; s < sp && col < max_vis; ++s, ++col) {
                buf[col].Char.UnicodeChar = L' ';
                WORD a = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                apply_sel(i, a); buf[col].Attributes = a;
            }
            ++i; continue;
        }
        // String
        if (wl[i] == L'"') {
            const WORD SA = FOREGROUND_RED | FOREGROUND_GREEN; // yellow
            WORD a = SA; apply_sel(i, a);
            buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = a;
            i++; col++;
            while (i < wl.size() && col < max_vis) {
                a = SA; apply_sel(i, a);
                buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = a;
                if (wl[i] == L'"') { i++; col++; break; }
                i++; col++;
            }
            continue;
        }
        // Comment
        if (wl[i] == L'\'') {
            while (i < wl.size() && col < max_vis) {
                WORD a = FOREGROUND_GREEN; apply_sel(i, a);
                buf[col].Char.UnicodeChar = wl[i++]; buf[col++].Attributes = a;
            }
            break;
        }
        // Number
        if (iswdigit(wl[i])) {
            size_t st = i;
            while (i < wl.size() && (iswdigit(wl[i]) || wl[i] == L'.')) ++i;
            for (size_t j = st; j < i && col < max_vis; ++j, ++col) {
                WORD a = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                apply_sel(j, a); buf[col].Char.UnicodeChar = wl[j]; buf[col].Attributes = a;
            }
            continue;
        }
        // Identifier / keyword / native function
        if (iswalpha(wl[i]) || wl[i] == L'_') {
            size_t st = i;
            while (i < wl.size() && (iswalnum(wl[i]) || wl[i] == L'_' || wl[i] == L'$')) ++i;
            std::wstring word = wl.substr(st, i - st);
            std::string aword = to_utf8(word);
            WORD kw_attr = BASE;
            if (is_kw(aword)) {
                // Language keyword: magenta (red + blue bright)
                kw_attr = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            } else if (is_native(aword)) {
                // Native/built-in function: cyan (green + blue bright)
                kw_attr = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            }
            for (size_t j = 0; j < word.length() && col < max_vis; ++j, ++col) {
                WORD a = kw_attr; apply_sel(st + j, a);
                buf[col].Char.UnicodeChar = word[j]; buf[col].Attributes = a;
            }
            continue;
        }
        // Other char
        WORD fa = BASE; apply_sel(i, fa);
        buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = fa;
        ++i; ++col;
    }

    // Extract visible portion
    std::vector<CHAR_INFO> screen_buf(screen_cols);
    for (int c = 0; c < screen_cols; ++c)
        screen_buf[c] = (left_col + c < (int)buf.size()) ? buf[left_col + c] : buf[0];

    WriteConsoleOutputW(hOut, screen_buf.data(), bufSz, bufCo, &wr);
}

void EditorImpl::draw_screen() {
    for (int row = 0; row < screen_rows; ++row) {
        int li = top_row + row;
        if (li < (int)lines_ref.size()) {
            draw_line(lines_ref[li], row, li);
        } else {
            set_cursor(row, 0);
            DWORD written;
            std::wstring empty = L"~" + std::wstring(screen_cols - 1, L' ');
            WriteConsoleW(hOut, empty.c_str(), (DWORD)empty.length(), &written, nullptr);
        }
    }
    std::wstring status = L" " + to_wide(filename.empty() ? "[new]" : filename);
    if (file_modified) status += L" *";
    status += L" | Ln:" + std::to_wstring(cy + 1) + L" Col:" + std::to_wstring(cx + 1);
    status += overwrite_mode ? L" | OVR" : L" | INS";
    status += L" | ^S:Save ^F:Find ^G:GoTo ^P:Paste ^Q:Exit";
    write_status(status);
}

// ── Navigation ───────────────────────────────────────────────

void EditorImpl::move_cursor(int dx, int dy) {
    cy = std::clamp(cy + dy, 0, (int)lines_ref.size() - 1);
    std::wstring wl = to_wide(lines_ref[cy]);
    cx = std::clamp(cx + dx, 0, (int)wl.length());
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
}

// ── File operations ──────────────────────────────────────────

void EditorImpl::save_file() {
    if (filename.empty()) {
        std::wstring wn = prompt(L"Save as: ");
        if (wn.empty()) { write_status(L"Save aborted."); return; }
        filename = to_utf8(wn);
    }
    std::ofstream out(filename);
    if (!out) { write_status(L"Error writing file!"); return; }
    for (size_t i = 0; i < lines_ref.size(); i++) {
        out << lines_ref[i];
        if (i + 1 < lines_ref.size()) out << "\n";
    }
    file_modified = false;
    write_status(L"File saved.");
}

void EditorImpl::find_text() {
    std::wstring query = prompt(L"Find: ");
    if (query.empty()) return;
    std::string q = to_utf8(query);
    for (size_t i = 0; i < lines_ref.size(); ++i) {
        size_t li = (cy + i) % lines_ref.size();
        size_t found = lines_ref[li].find(q, (li == (size_t)cy ? cx + 1 : 0));
        if (found != std::string::npos) {
            cy = (int)li; cx = (int)found;
            top_row = std::max(0, cy - screen_rows / 2);
            return;
        }
    }
    write_status(L"Not found: " + query);
}

void EditorImpl::go_to_line() {
    std::wstring input = prompt(L"Go to line: ");
    if (input.empty()) return;
    try {
        int line = std::stoi(input);
        if (line >= 1 && line <= (int)lines_ref.size()) {
            cy = line - 1; cx = 0;
            top_row = std::max(0, cy - screen_rows / 2);
        } else {
            write_status(L"Line out of range.");
        }
    } catch (...) { write_status(L"Invalid input."); }
}

void EditorImpl::paste_from_clipboard() {
    if (!OpenClipboard(NULL)) return;
    save_state();
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return; }
    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
    if (!pText) { CloseClipboard(); return; }
    std::wstring wtext(pText);
    GlobalUnlock(hData);
    CloseClipboard();

    std::string text = to_utf8(wtext);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    if (text.empty()) return;

    std::vector<std::string> new_lines;
    std::istringstream ss(text);
    std::string seg;
    while (std::getline(ss, seg, '\n')) new_lines.push_back(seg);
    if (!text.empty() && text.back() == '\n') new_lines.push_back("");
    if (new_lines.empty()) return;

    if (lines_ref.empty()) lines_ref.push_back("");
    if (cy >= (int)lines_ref.size()) cy = (int)lines_ref.size() - 1;

    std::wstring cwl = to_wide(lines_ref[cy]);
    if (cx > (int)cwl.length()) cx = (int)cwl.length();

    std::string prefix = to_utf8(cwl.substr(0, cx));
    std::string suffix = to_utf8(cwl.substr(cx));

    if (new_lines.size() == 1) {
        lines_ref[cy] = prefix + new_lines[0] + suffix;
        cx += (int)to_wide(new_lines[0]).length();
    } else {
        lines_ref[cy] = prefix + new_lines[0];
        lines_ref.insert(lines_ref.begin() + cy + 1, new_lines.begin() + 1, new_lines.end());
        int last = cy + (int)new_lines.size() - 1;
        lines_ref[last] += suffix;
        cy = last;
        cx = (int)to_wide(new_lines.back()).length();
    }

    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
    file_modified = true;
}

// ── Undo/Redo ────────────────────────────────────────────────

void EditorImpl::save_state() {
    undo_stack.push_back({ lines_ref, cx, cy });
    redo_stack.clear();
    if (undo_stack.size() > 100) undo_stack.erase(undo_stack.begin());
}

void EditorImpl::undo() {
    if (undo_stack.empty()) return;
    redo_stack.push_back({ lines_ref, cx, cy });
    auto st = undo_stack.back(); undo_stack.pop_back();
    lines_ref = st.lines; cx = st.cx; cy = st.cy;
    file_modified = true;
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
}

void EditorImpl::redo() {
    if (redo_stack.empty()) return;
    undo_stack.push_back({ lines_ref, cx, cy });
    auto st = redo_stack.back(); redo_stack.pop_back();
    lines_ref = st.lines; cx = st.cx; cy = st.cy;
    file_modified = true;
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
}

// ── Selection ────────────────────────────────────────────────

bool EditorImpl::has_selection() const {
    return is_selecting && (sel_cy != cy || sel_cx != cx);
}

void EditorImpl::get_selection(int& sx, int& sy, int& ex, int& ey) const {
    if (sel_cy < cy || (sel_cy == cy && sel_cx < cx)) {
        sx = sel_cx; sy = sel_cy; ex = cx; ey = cy;
    } else {
        sx = cx; sy = cy; ex = sel_cx; ey = sel_cy;
    }
}

void EditorImpl::delete_selection() {
    if (!has_selection()) return;
    save_state();
    int sx, sy, ex, ey;
    get_selection(sx, sy, ex, ey);

    std::wstring wey = to_wide(lines_ref[ey]);
    std::wstring remain = wey.substr(ex);
    std::wstring wsy = to_wide(lines_ref[sy]);
    wsy = wsy.substr(0, sx) + remain;
    lines_ref[sy] = to_utf8(wsy);

    if (ey > sy)
        lines_ref.erase(lines_ref.begin() + sy + 1, lines_ref.begin() + ey + 1);

    cx = sx; cy = sy;
    is_selecting = false;
    file_modified = true;
}

void EditorImpl::copy_to_clipboard() {
    if (!has_selection()) return;
    int sx, sy, ex, ey;
    get_selection(sx, sy, ex, ey);

    std::wstring wsel;
    for (int i = sy; i <= ey; ++i) {
        std::wstring wl = to_wide(lines_ref[i]);
        int ls = (i == sy) ? sx : 0;
        int le = (i == ey) ? ex : (int)wl.length();
        wsel += wl.substr(ls, le - ls);
        if (i < ey) wsel += L"\r\n";
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wsel.length() + 1) * sizeof(wchar_t));
        if (hMem) {
            memcpy(GlobalLock(hMem), wsel.c_str(), (wsel.length() + 1) * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

// ── Prompt ───────────────────────────────────────────────────

std::wstring EditorImpl::prompt(const std::wstring& msg) {
    write_status(msg);
    set_cursor(screen_rows + 1, (SHORT)msg.length());
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hIn, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    wchar_t buf[256];
    DWORD read;
    ReadConsoleW(hIn, buf, 255, &read, nullptr);
    buf[read] = 0;
    std::wstring input = buf;
    input.erase(std::remove(input.begin(), input.end(), L'\r'), input.end());
    input.erase(std::remove(input.begin(), input.end(), L'\n'), input.end());
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    return input;
}

// ── Public interface ─────────────────────────────────────────

Editor::Editor(std::vector<std::string>& lines, const std::string& filename)
    : lines_ref(lines), filename(filename) {}

void Editor::run() {
    run_requested = false;
    EditorImpl impl(lines_ref, filename, &run_requested);
    impl.run();
}
#endif // _WIN32
