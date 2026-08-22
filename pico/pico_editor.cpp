// A full-screen editor over the 40 by 40 console: EDIT "name" at the
// prompt opens it, Ctrl-S writes the file back, Ctrl-Q (or the ESC
// key on the device) leaves. It speaks plain ANSI, so the same code
// serves the panel and a USB terminal.
//
// Painting is stingy: moving the cursor paints nothing (the console
// walks its own inverted cell on cursor moves), editing a line paints
// that line, and only structure changes repaint from the edit
// downward - never a clear, so nothing flickers.

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

extern "C" int repl_read_key(void);

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

#define ED_COLS 40
#define ED_ROWS 39   // last row is the status line

static void cup(int row, int col) { printf("\x1b[%d;%dH", row + 1, col + 1); }

static void draw_status(const char* name, int line, int total, bool dirty,
                        int cx, int top) {
    cup(ED_ROWS, 0);
    printf("\x1b[K%c%s %d/%d c%d t%d ^S ^Q", dirty ? '*' : ' ', name,
           line + 1, total, cx, top);
}

static void draw_line(const std::string& s, int screen_row, int off) {
    cup(screen_row, 0);
    printf("\x1b[K");
    // printf only: the SDK's printf writes straight to stdio while the
    // newlib FILE calls buffer, and mixing the two reorders the frame.
    if ((int)s.size() > off) {
        int n = (int)((s.size() - off) > ED_COLS ? ED_COLS : s.size() - off);
        printf("%.*s", n, s.c_str() + off);
    }
}

void pico_editor(const char* name) {
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

    for (;;) {
        if (cy < top) { top = cy; paint_from = 0; }
        if (cy >= top + ED_ROWS) { top = cy - ED_ROWS + 1; paint_from = 0; }
        int xoff = (cx / ED_COLS) * ED_COLS;
        if (xoff != last_xoff) { paint_line = true; last_xoff = xoff; }

        if (paint_from < ED_ROWS) {
            for (int r = paint_from; r < ED_ROWS; r++) {
                if (top + r < (int)lines.size())
                    draw_line(lines[top + r], r, r == cy - top ? xoff : 0);
                else {
                    cup(r, 0);
                    printf("\x1b[K");
                }
            }
        } else if (paint_line) {
            draw_line(lines[cy], cy - top, xoff);
        }
        paint_from = ED_ROWS;
        paint_line = false;

        draw_status(name, cy, (int)lines.size(), dirty, cx, top);
        cup(cy - top, cx - xoff);
        fflush(NULL);

        int c = repl_read_key();
        std::string& ln = lines[cy];

        if (c == 17 || c == K_ESC) {           // Ctrl-Q
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
        if (c == K_UP)    { if (cy > 0) { cy--; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); } continue; }
        if (c == K_DOWN)  { if (cy + 1 < (int)lines.size()) { cy++; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); } continue; }
        if (c == K_PGUP)  { cy = cy > ED_ROWS ? cy - ED_ROWS : 0; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); continue; }
        if (c == K_PGDN)  { cy += ED_ROWS; if (cy >= (int)lines.size()) cy = lines.size() - 1; if (cx > (int)lines[cy].size()) cx = lines[cy].size(); continue; }
        if (c == K_LEFT)  { if (cx > 0) cx--; else if (cy > 0) { cy--; cx = lines[cy].size(); } continue; }
        if (c == K_RIGHT) { if (cx < (int)ln.size()) cx++; else if (cy + 1 < (int)lines.size()) { cy++; cx = 0; } continue; }
        if (c == K_HOME)  { cx = 0; continue; }
        if (c == K_END)   { cx = ln.size(); continue; }
        if (c == '\r' || c == '\n') {
            std::string rest = ln.substr(cx);
            ln.erase(cx);
            lines.insert(lines.begin() + cy + 1, rest);
            paint_from = cy - top;
            cy++; cx = 0;
            dirty = true;
            continue;
        }
        if (c == 8 || c == 127) {
            if (cx > 0) {
                ln.erase(cx - 1, 1);
                cx--;
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
            if (cx < (int)ln.size()) {
                ln.erase(cx, 1);
                dirty = true; paint_line = true;
            } else if (cy + 1 < (int)lines.size()) {
                ln += lines[cy + 1];
                lines.erase(lines.begin() + cy + 1);
                dirty = true;
                paint_from = cy - top;
            }
            continue;
        }
        if (c >= 32 && c < 127) {
            ln.insert(ln.begin() + cx, (char)c);
            cx++;
            dirty = true; paint_line = true;
        }
    }
}
