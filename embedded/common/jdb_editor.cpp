// A full-screen editor over the console: EDIT "name" at the prompt opens
// it, Ctrl-S writes the file back, Ctrl-Q leaves - asking first if there
// is anything to lose. It speaks plain ANSI, so the same code serves the
// panel, the board's own screen and a USB terminal. F1 lists the keys.
//
// Shift and an arrow starts a selection and extends it; Ctrl-C, Ctrl-X
// and Ctrl-V copy, cut and paste. The clipboard outlives the editor, so
// a passage can be carried from one file to another.
//
// Ctrl-F asks for text and selects the next place it occurs, Ctrl-G the
// one after that, Ctrl-T replaces. The search ignores case and wraps at
// the end. Ctrl-Z takes back the last change; a run of typing on one
// line counts as one change.
//
// Ctrl-R writes the file and hands it to the prompt to run. The prompt
// opens the editor again afterwards, on the line an error named.
//
// A line longer than the page scrolls the whole page sideways rather
// than only its own row, so the columns of neighbouring lines stay
// aligned; the view shifts when the cursor would leave it. A row that
// goes on past the right edge ends in a marker.
//
// Painting is stingy: moving the cursor paints nothing (the console walks
// its own cell), editing a line paints that line, and only structure
// changes repaint from the edit downward - never a clear, so nothing
// flickers. A live selection is the exception and repaints the page,
// because the highlight moves under text the cursor never touched.

#include <stdio.h>
#include <stdlib.h>
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

static int snap(const std::string& s, int i) {
    if (i > (int)s.size()) i = (int)s.size();
    while (i > 0 && i < (int)s.size() && utf8_is_cont((unsigned char)s[i])) i--;
    return i;
}

// The lead byte has been read; the rest of the sequence follows on the
// same line.
static std::string read_sequence(int lead) {
    std::string seq(1, (char)lead);
    int n = utf8_seq_len((unsigned char)lead);
    for (int i = 1; i < n; i++) {
        int c = repl_read_key();
        if (c < 0x80 || c > 0xBF) break;
        seq += (char)c;
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
#define K_CLEFT  0xBC
#define K_CRIGHT 0xBD
#define K_CHOME  0xDA
#define K_CEND   0xDB
#define K_F1     0xC1
#define K_STAB   0xC2

#define INDENT 2

// What a word is made of, for the word jumps: a name, with its dots
// and its type suffix, or a number.
static bool word_byte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '$' || c >= 0x80;
}

// The console decides how big the page is: 40 by 40 on the PicoCalc, 40
// by 30 on the 2.8 inch panel. The port answers, the editor adapts.
extern "C" void jdb_con_size(int* cols, int* rows);

static int ED_COLS = 40;
static int ED_ROWS = 39;   // last row is the status line

// Outlive one editing session on purpose: the clipboard so a passage can
// be cut here and pasted there, the search text so Ctrl-G carries on,
// the cursor so a run-and-return lands where it left.
static std::string g_clip;
static std::string g_find;
static std::string g_last_name;
static int g_last_cy = 0, g_last_cx = 0;

static void cup(int row, int col) { printf("\x1b[%d;%dH", row + 1, col + 1); }

static void draw_status(const char* name, int line, int total, bool dirty,
                        int cx, int xview, bool sel, const char* msg) {
    cup(ED_ROWS, 0);
    if (msg && *msg) { printf("\x1b[K%s", msg); return; }
    printf("\x1b[K%c%s %d/%d c%d%s%s F1", dirty ? '*' : ' ', name,
           line + 1, total, cx, xview ? " <" : "", sel ? " SEL" : "");
}

static int fold(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// Byte index of needle in hay at or after from, ignoring ASCII case;
// -1 if absent.
static int find_in(const std::string& hay, const std::string& needle, int from) {
    int n = (int)needle.size(), h = (int)hay.size();
    if (n == 0 || from < 0) return -1;
    for (int i = from; i + n <= h; i++) {
        int k = 0;
        while (k < n && fold((unsigned char)hay[i + k]) == fold((unsigned char)needle[k])) k++;
        if (k == n) return i;
    }
    return -1;
}

static int indent_of(const std::string& s) {
    int i = 0;
    while (i < (int)s.size() && s[i] == ' ') i++;
    return i;
}

// selfrom/selto are byte columns within the line; an empty range means
// nothing on this row is selected. cursor is the screen column the
// cursor sits on in this row, or -1, so the marker never covers it.
static void draw_line(const std::string& s, int screen_row, int off,
                      int selfrom, int selto, int cursor) {
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
    } else {
        if (a > 0) syntax_print(s.c_str() + off, a);
        printf("\x1b[7m%.*s\x1b[27m", b - a, s.c_str() + off + a);
        if (b < n) syntax_print(s.c_str() + off + b, n - b);
    }
    if (end < (int)s.size() && cursor != ED_COLS - 1) {
        cup(screen_row, ED_COLS - 1);
        printf("\x1b[7m>\x1b[27m");
    }
}

// A line of input on the status row. Enter keeps it, Escape drops it.
static bool prompt_line(const char* label, std::string& s) {
    for (;;) {
        cup(ED_ROWS, 0);
        printf("\x1b[K%s%s", label, s.c_str());
        fflush(NULL);
        int c = repl_read_key();
        if (c == '\r' || c == '\n') return true;
        if (c == K_ESC || c == 17 || c == 3) return false;
        if (c == 8 || c == 127) {
            if (!s.empty()) s.erase(prev_cp(s, (int)s.size()));
            continue;
        }
        if (c >= 32 && c < 255)
            s += c >= 0xC2 ? read_sequence(c) : std::string(1, (char)c);
    }
}

// A question on the status line, answered with a single key.
static int ask(const char* q) {
    cup(ED_ROWS, 0);
    printf("\x1b[K%s", q);
    fflush(NULL);
    return repl_read_key();
}

static void help_page(void) {
    static const char* const rows[] = {
        "jdBasic editor",
        "",
        "^S save    ^Q quit    ^R save+run",
        "^F find    ^G next    ^T replace",
        "^L go to line         ^Z undo",
        "^A select all   Shift+arrow select",
        "^C copy   ^X cut   ^V paste",
        "^D duplicate line   ^K delete line",
        "Tab indent   Shift-Tab outdent",
        "Ctrl+arrow word   Ctrl+Home/End file",
        "F1 or ^H this page",
        "",
        "any key returns",
    };
    printf("\x1b[2J");
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        cup((int)i + 1, 1);
        printf("%s", rows[i]);
    }
    fflush(NULL);
    repl_read_key();
}

static bool write_file(const char* name, const std::vector<std::string>& lines) {
    FILE* w = fopen(name, "w");
    if (!w) return false;
    for (auto& s : lines) { fputs(s.c_str(), w); fputc('\n', w); }
    fclose(w);
    return true;
}

// One change: the lines it replaced, and how many stand there now.
struct UndoRec {
    int y;
    int n_after;
    std::vector<std::string> before;
    int cx, cy;
};

int jdb_editor(const char* name, int goto_line) {
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
    if (goto_line > 0) {
        cy = goto_line - 1;
    } else if (g_last_name == name) {
        cy = g_last_cy; cx = g_last_cx;
    }
    if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
    if (cx > (int)lines[cy].size()) cx = (int)lines[cy].size();

    bool dirty = false;
    int paint_from = 0;        // first screen row to repaint; ED_ROWS = none
    bool paint_line = false;   // repaint just the cursor line
    int xview = 0;             // first column on screen, for every row
    std::string msg;           // shown on the status row until the next key

    int ay = -1, ax = 0;       // selection anchor; ay < 0 means none

    std::vector<UndoRec> undo;
    size_t size_before = 0;
    bool last_merged = false;

    // Called before lines y0 .. y0+n-1 change. A single-line change may
    // merge into the previous record of the same line, so typing undoes
    // as a run.
    auto begin_edit = [&](int y0, int n, bool merge) {
        size_before = lines.size();
        if (merge && last_merged && !undo.empty() && undo.back().y == y0 &&
            undo.back().n_after == 1 && undo.back().before.size() == 1)
            return;
        UndoRec r;
        r.y = y0; r.n_after = n; r.cx = cx; r.cy = cy;
        for (int i = 0; i < n && y0 + i < (int)lines.size(); i++) r.before.push_back(lines[y0 + i]);
        if (undo.size() >= 16) undo.erase(undo.begin());
        undo.push_back(r);
        last_merged = merge;
    };
    auto end_edit = [&]() {
        if (undo.empty()) return;
        undo.back().n_after = (int)undo.back().before.size() + (int)lines.size() - (int)size_before;
        dirty = true;
    };

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
        begin_edit(y0, y1 - y0 + 1, false);
        if (y0 == y1) {
            lines[y0].erase(x0, x1 - x0);
        } else {
            lines[y0].erase(x0);
            lines[y0] += lines[y1].substr(x1);
            lines.erase(lines.begin() + y0 + 1, lines.begin() + y1 + 1);
        }
        cy = y0; cx = x0;
        ay = -1;
        end_edit();
    };

    auto anchor = [&](bool keep) {
        if (!keep) { ay = -1; return; }
        if (ay < 0) { ay = cy; ax = cx; }
    };

    // The rows a selection covers for a block operation: a selection
    // that ends at the start of a line does not include that line.
    auto sel_rows = [&](int& y0, int& y1) {
        int x0, x1;
        sel_span(y0, x0, y1, x1);
        if (y1 > y0 && x1 == 0) y1--;
    };

    // The next place the search text occurs after the cursor, wrapping
    // once; found text becomes the selection.
    auto find_next = [&]() {
        if (g_find.empty()) return false;
        int total = (int)lines.size();
        int y = cy, from = cx;
        for (int step = 0; step <= total; step++) {
            int hit = find_in(lines[y], g_find, from);
            if (hit >= 0) {
                ay = y; ax = hit;
                cy = y; cx = hit + (int)g_find.size();
                paint_from = 0;
                return true;
            }
            y = (y + 1) % total;
            from = 0;
        }
        msg = "not found: " + g_find;
        return false;
    };

    auto leave = [&]() {
        g_last_name = name; g_last_cy = cy; g_last_cx = cx;
        printf("\x1b[2J");
        cup(0, 0);
        fflush(NULL);
    };

    for (;;) {
        if (cy < top) { top = cy; paint_from = 0; }
        if (cy >= top + ED_ROWS) { top = cy - ED_ROWS + 1; paint_from = 0; }
        cx = snap(lines[cy], cx);
        int cxcol = cols_to(lines[cy], cx);
        if (cxcol < xview) { xview = cxcol > 8 ? cxcol - 8 : 0; paint_from = 0; }
        if (cxcol >= xview + ED_COLS) { xview = cxcol - ED_COLS + 8; paint_from = 0; }

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
                    draw_line(lines[y], r, byte_at(lines[y], xview), from, to,
                              y == cy ? cxcol - xview : -1);
                } else {
                    cup(r, 0);
                    printf("\x1b[K");
                }
            }
        } else if (paint_line) {
            draw_line(lines[cy], cy - top, byte_at(lines[cy], xview), 0, 0, cxcol - xview);
        }
        paint_from = ED_ROWS;
        paint_line = false;

        draw_status(name, cy, (int)lines.size(), dirty, cxcol, xview, sel, msg.c_str());
        msg.clear();
        cup(cy - top, cxcol - xview);
        fflush(NULL);

        int c = repl_read_key();
        std::string& ln = lines[cy];

        if (c == K_F1) {
            help_page();
            paint_from = 0;
            continue;
        }
        if (c == 17 || c == K_ESC) {            // Ctrl-Q
            if (dirty) {
                int a = fold(ask("save changes? y=save n=discard esc=back "));
                if (a != 'y' && a != 'n') { paint_from = 0; continue; }
                if (a == 'y' && !write_file(name, lines)) {
                    msg = "cannot write";
                    paint_from = 0;
                    continue;
                }
            }
            leave();
            return 0;
        }
        if (c == 19) {                          // Ctrl-S
            if (write_file(name, lines)) dirty = false;
            else msg = "cannot write";
            continue;
        }
        if (c == 18) {                          // Ctrl-R
            if (!write_file(name, lines)) { msg = "cannot write"; continue; }
            dirty = false;
            leave();
            return 1;
        }
        if (c == 26) {                          // Ctrl-Z
            if (undo.empty()) { msg = "nothing to undo"; continue; }
            UndoRec r = undo.back();
            undo.pop_back();
            last_merged = false;
            lines.erase(lines.begin() + r.y, lines.begin() + r.y + r.n_after);
            lines.insert(lines.begin() + r.y, r.before.begin(), r.before.end());
            if (lines.empty()) lines.push_back("");
            cy = r.cy < (int)lines.size() ? r.cy : (int)lines.size() - 1;
            cx = r.cx;
            ay = -1;
            dirty = true;
            paint_from = 0;
            continue;
        }
        if (c == 6) {                           // Ctrl-F
            std::string want = g_find;
            if (prompt_line("find: ", want) && !want.empty()) {
                g_find = want;
                find_next();
            }
            paint_from = 0;
            continue;
        }
        if (c == 7) {                           // Ctrl-G
            if (g_find.empty()) {
                std::string want;
                if (prompt_line("find: ", want) && !want.empty()) g_find = want;
            }
            find_next();
            paint_from = 0;
            continue;
        }
        if (c == 20) {                          // Ctrl-T
            std::string want = g_find, with;
            paint_from = 0;
            if (!prompt_line("replace: ", want) || want.empty()) continue;
            if (!prompt_line("with: ", with)) continue;
            g_find = want;
            ay = -1;
            int done = 0;
            bool all = false, stop = false;
            int start_y = cy, start_x = cx;
            // One pass from the cursor to the end, then from the top back
            // to where it began.
            for (int pass = 0; pass < 2 && !stop; pass++) {
                int y = pass == 0 ? cy : 0;
                int from = pass == 0 ? cx : 0;
                int last = pass == 0 ? (int)lines.size() - 1 : start_y;
                for (; y <= last && !stop; y++, from = 0) {
                    for (;;) {
                        int hit = find_in(lines[y], want, from);
                        if (hit < 0) break;
                        if (pass == 1 && y == start_y && hit >= start_x) break;
                        if (!all) {
                            ay = y; ax = hit; cy = y; cx = hit + (int)want.size();
                            if (cy < top || cy >= top + ED_ROWS) top = cy;
                            int hc = cols_to(lines[y], hit);
                            int vc = hc >= ED_COLS - 8 ? hc - 8 : 0;
                            for (int r = 0; r < ED_ROWS; r++) {
                                int yy = top + r;
                                if (yy < (int)lines.size())
                                    draw_line(lines[yy], r, byte_at(lines[yy], vc),
                                              yy == y ? hit : 0,
                                              yy == y ? hit + (int)want.size() : 0, -1);
                                else { cup(r, 0); printf("\x1b[K"); }
                            }
                            cup(ED_ROWS, 0);
                            printf("\x1b[Kreplace? y n a=all esc ");
                            fflush(NULL);
                            int k = fold(repl_read_key());
                            if (k == 'a') all = true;
                            else if (k == 'n') { from = hit + (int)want.size(); continue; }
                            else if (k != 'y') { stop = true; break; }
                        }
                        begin_edit(y, 1, true);
                        lines[y].replace(hit, want.size(), with);
                        end_edit();
                        done++;
                        from = hit + (int)with.size();
                        if (with.empty() && from >= (int)lines[y].size()) break;
                    }
                }
            }
            ay = -1;
            if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
            if (cx > (int)lines[cy].size()) cx = (int)lines[cy].size();
            char b[48];
            snprintf(b, sizeof b, "%d replaced", done);
            msg = b;
            xview = 0;
            continue;
        }
        if (c == 12) {                          // Ctrl-L
            std::string want;
            paint_from = 0;
            if (!prompt_line("line: ", want) || want.empty()) continue;
            int n = atoi(want.c_str());
            if (n < 1) n = 1;
            if (n > (int)lines.size()) n = (int)lines.size();
            anchor(false);
            cy = n - 1; cx = 0;
            continue;
        }
        if (c == 1) {                           // Ctrl-A
            ay = 0; ax = 0;
            cy = (int)lines.size() - 1; cx = (int)lines[cy].size();
            paint_from = 0;
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
            begin_edit(cy, 1, false);
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
            end_edit();
            paint_from = 0;
            continue;
        }
        if (c == 4) {                           // Ctrl-D
            anchor(false);
            begin_edit(cy, 1, false);
            lines.insert(lines.begin() + cy + 1, lines[cy]);
            end_edit();
            paint_from = cy - top;
            cy++;
            continue;
        }
        if (c == 11) {                          // Ctrl-K
            anchor(false);
            begin_edit(cy, 1, false);
            if (lines.size() == 1) lines[0].clear();
            else lines.erase(lines.begin() + cy);
            end_edit();
            if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
            cx = 0;
            paint_from = cy - top;
            continue;
        }
        if (c == 9 || c == K_STAB) {            // Tab, Shift-Tab
            if (has_sel()) {
                int y0, y1;
                sel_rows(y0, y1);
                begin_edit(y0, y1 - y0 + 1, false);
                for (int y = y0; y <= y1; y++) {
                    if (c == 9) lines[y].insert(0, INDENT, ' ');
                    else {
                        int k = indent_of(lines[y]);
                        lines[y].erase(0, k < INDENT ? k : INDENT);
                    }
                }
                end_edit();
                // Keep the selection on the same rows.
                if (ax > (int)lines[ay].size()) ax = (int)lines[ay].size();
                if (cx > (int)lines[cy].size()) cx = (int)lines[cy].size();
                paint_from = 0;
                continue;
            }
            if (c == 9) {
                begin_edit(cy, 1, true);
                int n = INDENT - (cxcol % INDENT);
                ln.insert(cx, n, ' ');
                cx += n;
                end_edit();
                paint_line = true;
            } else {
                int k = indent_of(ln);
                if (k > 0) {
                    begin_edit(cy, 1, false);
                    int d = k < INDENT ? k : INDENT;
                    ln.erase(0, d);
                    cx = cx > d ? cx - d : 0;
                    end_edit();
                    paint_line = true;
                }
            }
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
        if (c == K_CHOME) { anchor(false); cy = 0; cx = 0; paint_from = 0; continue; }
        if (c == K_CEND)  { anchor(false); cy = lines.size() - 1; cx = lines[cy].size(); paint_from = 0; continue; }
        if (c == K_CRIGHT) {
            anchor(false);
            if (cx >= (int)ln.size()) {
                if (cy + 1 < (int)lines.size()) { cy++; cx = 0; }
            } else {
                while (cx < (int)ln.size() && word_byte(ln[cx])) cx++;
                while (cx < (int)ln.size() && !word_byte(ln[cx])) cx++;
            }
            paint_from = 0; continue;
        }
        if (c == K_CLEFT) {
            anchor(false);
            if (cx == 0) {
                if (cy > 0) { cy--; cx = lines[cy].size(); }
            } else {
                while (cx > 0 && !word_byte(ln[cx - 1])) cx--;
                while (cx > 0 && word_byte(ln[cx - 1])) cx--;
            }
            paint_from = 0; continue;
        }
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
            begin_edit(cy, 1, false);
            std::string& cur = lines[cy];
            int ind = indent_of(cur);
            if (ind > cx) ind = cx;
            std::string rest = cur.substr(cx);
            cur.erase(cx);
            lines.insert(lines.begin() + cy + 1, std::string(ind, ' ') + rest);
            end_edit();
            paint_from = cy - top;
            cy++; cx = ind;
            continue;
        }
        if (c == 8 || c == 127) {
            if (has_sel()) { sel_drop(); paint_from = 0; continue; }
            if (cx > 0) {
                begin_edit(cy, 1, true);
                int p = prev_cp(ln, cx);
                ln.erase(p, cx - p);
                cx = p;
                end_edit();
                paint_line = true;
            } else if (cy > 0) {
                begin_edit(cy - 1, 2, false);
                cx = lines[cy - 1].size();
                lines[cy - 1] += ln;
                lines.erase(lines.begin() + cy);
                cy--;
                end_edit();
                paint_from = cy - top;
            }
            continue;
        }
        if (c == K_DEL) {
            if (has_sel()) { sel_drop(); paint_from = 0; continue; }
            if (cx < (int)ln.size()) {
                begin_edit(cy, 1, true);
                ln.erase(cx, next_cp(ln, cx) - cx);
                end_edit();
                paint_line = true;
            } else if (cy + 1 < (int)lines.size()) {
                begin_edit(cy, 2, false);
                ln += lines[cy + 1];
                lines.erase(lines.begin() + cy + 1);
                end_edit();
                paint_from = cy - top;
            }
            continue;
        }
        if (c >= 32 && c < 255) {
            if (has_sel()) { sel_drop(); paint_from = 0; }
            begin_edit(cy, 1, true);
            std::string seq = c >= 0xC2 ? read_sequence(c) : std::string(1, (char)c);
            lines[cy].insert(cx, seq);
            cx += (int)seq.size();
            end_edit();
            if (paint_from >= ED_ROWS) paint_line = true;
        }
    }
}
