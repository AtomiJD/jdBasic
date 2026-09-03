// A full-screen editor over the console: EDIT "name" at the prompt opens
// it, Ctrl-S writes the file back, Ctrl-Q leaves - asking first if there
// is anything to lose. It speaks plain ANSI, so the same code serves the
// panel, the board's own screen and a USB terminal.
//
// Shift and an arrow starts a selection and extends it; Ctrl-C, Ctrl-X
// and Ctrl-V copy, cut and paste. The clipboard outlives the editor, so
// a passage can be carried from one file to another.
//
// Painting is stingy: moving the cursor paints nothing (the console walks
// its own cell), editing a line paints that line, and only structure
// changes repaint from the edit downward - never a clear, so nothing
// flickers. A live selection is the exception and repaints the page,
// because the highlight moves under text the cursor never touched.

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include "jdb_utf8.h"

extern "C" int repl_read_key(void);

// Positions in a line are bytes; the screen counts code points. These
// step and measure in bytes over UTF-8 sequences.
static int prev_cp(const std::string& s, int i) { return utf8_prev(s.c_str(), i); }
static int next_cp(const std::string& s, int i) { return utf8_next(s.c_str(), (int)s.size(), i); }
static int cols_to(const std::string& s, int i) { return utf8_cols(s.c_str(), 0, i); }
static int byte_at(const std::string& s, int col) { return utf8_byte_at(s.c_str(), (int)s.size(), col); }
// A byte position inside a sequence is moved back to its lead byte.
static int snap(const std::string& s, int i) {
    if (i > (int)s.size()) i = (int)s.size();
    while (i > 0 && i < (int)s.size() && utf8_is_cont((unsigned char)s[i])) i--;
    return i;
}

// The rest of a sequence whose lead byte was just read: its continuation
// bytes follow in the queue.
static std::string read_sequence(int lead) {
    std::string seq(1, (char)lead);
    int n = utf8_seq_len((unsigned char)lead);
    for (int k = 1; k < n; k++) {
        int b = repl_read_key();
        if (b < 0x80 || b > 0xBF) break;
        seq += (char)b;
    }
    return seq;
}
void syntax_print(const char* s, int n);

#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5
#define K_PGUP  0xD6
#define K_PGDN  0xD7
#define K_ESC   0xB1

#define K_SLEFT  0xB8
#define K_SUP    0xB9
#define K_SDOWN  0xBA
#define K_SRIGHT 0xBB
#define K_SHOME  0xD8
#define K_SEND   0xD9

// The console decides how big the page is: 40 by 40 on the PicoCalc, 40
// by 30 on the 2.8 inch panel. The port answers, the editor adapts.
extern "C" void jdb_con_size(int* cols, int* rows);

static int ED_COLS = 40;
static int ED_ROWS = 39;   // last row is the status line

// Outlives one editing session on purpose: that is what makes it possible
// to cut here and paste there.
static std::string g_clip;

static void cup(int row, int col) { printf("\x1b[%d;%dH", row + 1, col + 1); }

static void draw_status(const char* name, int line, int total, bool dirty,
                        int cx, bool sel) {
    cup(ED_ROWS, 0);
    printf("\x1b[K%c%s %d/%d c%d%s ^S ^Q", dirty ? '*' : ' ', name,
           line + 1, total, cx, sel ? " SEL" : "");
}

// selfrom/selto are columns within the line, in buffer coordinates; an
// empty range means nothing on this row is selected.
static void draw_line(const std::string& s, int screen_row, int off,
                      int selfrom, int selto) {
    cup(screen_row, 0);
    printf("\x1b[K");
    if ((int)s.size() <= off) return;
    // As many bytes as fill the row's columns.
    int end = off, cols = 0;
    while (end < (int)s.size() && cols < ED_COLS) { end = next_cp(s, end); cols++; }
    int n = end - off;

    int a = selfrom - off, b = selto - off;
    if (a < 0) a = 0;
    if (b > n) b = n;
    if (a >= b) {                       // nothing selected on this row
        syntax_print(s.c_str() + off, n);
        return;
    }
    if (a > 0) syntax_print(s.c_str() + off, a);
    printf("\x1b[7m%.*s\x1b[27m", b - a, s.c_str() + off + a);
    if (b < n) syntax_print(s.c_str() + off + b, n - b);
}

// A question on the status line, answered with a single key.
static int ask(const char* q) {
    cup(ED_ROWS, 0);
    printf("\x1b[K%s", q);
    fflush(NULL);
    return repl_read_key();
}

void jdb_editor(const char* name) {
    {
        int c, r;
        jdb_con_size(&c, &r);
        if (c > 0) ED_COLS = c;
        if (r > 1) ED_ROWS = r - 1;
    }

    std::vector<std::string> lines;
    FILE* f = fopen(name, "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof buf, f)) {
            size_t n = strlen(buf);
            while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
            lines.push_back(buf);
        }
        fclose(f);
    }
    if (lines.empty()) lines.push_back("");

    int cy = 0, cx = 0, top = 0;
    bool dirty = false;
    int paint_from = 0;        // first screen row to repaint; ED_ROWS = none
    bool paint_line = false;   // repaint just the cursor line
    int last_xoff = 0;

    int ay = -1, ax = 0;       // selection anchor; ay < 0 means none

    auto has_sel = [&]() { return ay >= 0 && (ay != cy || ax != cx); };

    // The anchor may sit after the cursor, so hand back an ordered pair.
    auto sel_span = [&](int& y0, int& x0, int& y1, int& x1) {
        if (ay < cy || (ay == cy && ax <= cx)) { y0 = ay; x0 = ax; y1 = cy; x1 = cx; }
        else { y0 = cy; x0 = cx; y1 = ay; x1 = ax; }
    };

    auto sel_text = [&]() {
        int y0, x0, y1, x1;
        sel_span(y0, x0, y1, x1);
        std::string out;
        if (y0 == y1) return lines[y0].substr(x0, x1 - x0);
        out = lines[y0].substr(x0);
        for (int y = y0 + 1; y < y1; y++) { out += '\n'; out += lines[y]; }
        out += '\n';
        out += lines[y1].substr(0, x1);
        return out;
    };

    auto sel_drop = [&]() {
        int y0, x0, y1, x1;
        sel_span(y0, x0, y1, x1);
        if (y0 == y1) {
            lines[y0].erase(x0, x1 - x0);
        } else {
            lines[y0].erase(x0);
            lines[y0] += lines[y1].substr(x1);
            lines.erase(lines.begin() + y0 + 1, lines.begin() + y1 + 1);
        }
        cy = y0; cx = x0;
        ay = -1;
        dirty = true;
    };

    auto anchor = [&](bool keep) {
        if (!keep) { ay = -1; return; }
        if (ay < 0) { ay = cy; ax = cx; }
    };

    for (;;) {
        if (cy < top) { top = cy; paint_from = 0; }
        if (cy >= top + ED_ROWS) { top = cy - ED_ROWS + 1; paint_from = 0; }
        cx = snap(lines[cy], cx);
        int cxcol = cols_to(lines[cy], cx);
        int xcol = (cxcol / ED_COLS) * ED_COLS;
        int xoff = byte_at(lines[cy], xcol);
        if (xoff != last_xoff) { paint_line = true; last_xoff = xoff; }

        int sy0 = 0, sx0 = 0, sy1 = -1, sx1 = 0;
        bool sel = has_sel();
        if (sel) sel_span(sy0, sx0, sy1, sx1);

        if (paint_from < ED_ROWS) {
            for (int r = paint_from; r < ED_ROWS; r++) {
                int y = top + r;
                if (y < (int)lines.size()) {
                    int from = 0, to = 0;
                    if (sel && y >= sy0 && y <= sy1) {
                        from = (y == sy0) ? sx0 : 0;
                        to   = (y == sy1) ? sx1 : (int)lines[y].size();
                    }
                    draw_line(lines[y], r, r == cy - top ? xoff : 0, from, to);
                } else {
                    cup(r, 0);
                    printf("\x1b[K");
                }
            }
        } else if (paint_line) {
            draw_line(lines[cy], cy - top, xoff, 0, 0);
        }
        paint_from = ED_ROWS;
        paint_line = false;

        draw_status(name, cy, (int)lines.size(), dirty, cxcol, sel);
        cup(cy - top, cxcol - xcol);
        fflush(NULL);

        int c = repl_read_key();
        std::string& ln = lines[cy];

        if (c == 17 || c == K_ESC) {            // Ctrl-Q
            if (dirty) {
                int a = ask("save changes? y=save n=discard esc=back ");
                if (a == K_ESC || a == 17) { paint_from = 0; continue; }
                if (a == 'y' || a == 'Y') {
                    FILE* w = fopen(name, "w");
                    if (!w) {
                        ask("cannot write - press a key ");
                        paint_from = 0;
                        continue;
                    }
                    for (auto& s : lines) { fputs(s.c_str(), w); fputc('\n', w); }
                    fclose(w);
                }
            }
            printf("\x1b[2J");
            cup(0, 0);
            fflush(NULL);
            return;
        }
        if (c == 19) {                          // Ctrl-S
            FILE* w = fopen(name, "w");
            if (w) {
                for (auto& s : lines) { fputs(s.c_str(), w); fputc('\n', w); }
                fclose(w);
                dirty = false;
            }
            continue;
        }
        if (c == 3 || c == 24) {                // Ctrl-C, Ctrl-X
            if (has_sel()) {
                g_clip = sel_text();
                if (c == 24) { sel_drop(); }
                else { ay = -1; }
                paint_from = 0;
            }
            continue;
        }
        if (c == 22) {                          // Ctrl-V
            if (g_clip.empty()) continue;
            if (has_sel()) sel_drop();
            std::string& cur = lines[cy];
            std::string tail = cur.substr(cx);
            cur.erase(cx);
            size_t i = 0;
            bool first = true;
            while (i <= g_clip.size()) {
                size_t nl = g_clip.find('\n', i);
                std::string piece = g_clip.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
                if (first) { lines[cy] += piece; cx = lines[cy].size(); first = false; }
                else {
                    cy++;
                    lines.insert(lines.begin() + cy, piece);
                    cx = piece.size();
                }
                if (nl == std::string::npos) break;
                i = nl + 1;
            }
            lines[cy] += tail;
            dirty = true;
            paint_from = 0;
            continue;
        }

        // Movement. The shifted twin of each key keeps the anchor, the
        // plain one drops it.
        if (c == K_UP || c == K_SUP) {
            anchor(c == K_SUP);
            if (cy > 0) { cy--; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); }
            paint_from = 0; continue;
        }
        if (c == K_DOWN || c == K_SDOWN) {
            anchor(c == K_SDOWN);
            if (cy + 1 < (int)lines.size()) { cy++; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); }
            paint_from = 0; continue;
        }
        if (c == K_LEFT || c == K_SLEFT) {
            anchor(c == K_SLEFT);
            if (cx > 0) cx = prev_cp(ln, cx); else if (cy > 0) { cy--; cx = lines[cy].size(); }
            paint_from = 0; continue;
        }
        if (c == K_RIGHT || c == K_SRIGHT) {
            anchor(c == K_SRIGHT);
            if (cx < (int)ln.size()) cx = next_cp(ln, cx); else if (cy + 1 < (int)lines.size()) { cy++; cx = 0; }
            paint_from = 0; continue;
        }
        if (c == K_HOME || c == K_SHOME) { anchor(c == K_SHOME); cx = 0; paint_from = 0; continue; }
        if (c == K_END  || c == K_SEND)  { anchor(c == K_SEND); cx = ln.size(); paint_from = 0; continue; }
        if (c == K_PGUP) {
            anchor(false);
            cy = cy > ED_ROWS ? cy - ED_ROWS : 0;
            if (cx > (int)lines[cy].size()) cx = lines[cy].size();
            continue;
        }
        if (c == K_PGDN) {
            anchor(false);
            cy += ED_ROWS;
            if (cy >= (int)lines.size()) cy = lines.size() - 1;
            if (cx > (int)lines[cy].size()) cx = lines[cy].size();
            continue;
        }

        // Anything that writes replaces the selection first.
        if (c == '\r' || c == '\n') {
            if (has_sel()) { sel_drop(); paint_from = 0; }
            std::string& cur = lines[cy];
            std::string rest = cur.substr(cx);
            cur.erase(cx);
            lines.insert(lines.begin() + cy + 1, rest);
            paint_from = cy - top;
            cy++; cx = 0;
            dirty = true;
            continue;
        }
        if (c == 8 || c == 127) {
            if (has_sel()) { sel_drop(); paint_from = 0; continue; }
            if (cx > 0) {
                int p = prev_cp(ln, cx);
                ln.erase(p, cx - p);
                cx = p;
                dirty = true; paint_line = true;
            } else if (cy > 0) {
                cx = lines[cy - 1].size();
                lines[cy - 1] += ln;
                lines.erase(lines.begin() + cy);
                cy--;
                dirty = true;
                paint_from = cy - top;
            }
            continue;
        }
        if (c == K_DEL) {
            if (has_sel()) { sel_drop(); paint_from = 0; continue; }
            if (cx < (int)ln.size()) {
                ln.erase(cx, next_cp(ln, cx) - cx);
                dirty = true; paint_line = true;
            } else if (cy + 1 < (int)lines.size()) {
                ln += lines[cy + 1];
                lines.erase(lines.begin() + cy + 1);
                dirty = true;
                paint_from = cy - top;
            }
            continue;
        }
        if (c >= 32 && c < 255) {
            if (has_sel()) { sel_drop(); paint_from = 0; }
            std::string seq = c >= 0xC2 ? read_sequence(c) : std::string(1, (char)c);
            lines[cy].insert(cx, seq);
            cx += (int)seq.size();
            dirty = true;
            if (paint_from >= ED_ROWS) paint_line = true;
        }
    }
}
