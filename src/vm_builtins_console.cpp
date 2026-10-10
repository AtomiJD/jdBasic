// VM builtins: screen, keyboard, clipboard and diagnostic output.

#include "vm_internal.h"

#ifdef __EMSCRIPTEN__
extern "C" int jdb_poll_key_js(void);   // EM_JS, defined in vm.cpp
#endif


#if defined(_WIN32)
// One keypress as the character it is. _getch answers a single byte, which is
// a codepage byte for anything past ASCII: an umlaut read key by key came out
// as a byte that is not valid UTF-8 on its own, so a password typed with one
// was broken before it was ever checked.
//
// The function and arrow keys keep the two-call protocol callers read: the 0
// or 0xE0 prefix passes through as the byte it was and the next call answers
// the scan code.
static std::string read_console_char() {
    int wch = _getwch();
    if (wch == 0 || wch == 0xE0) return std::string(1, (char)wch);
    wchar_t w[2] = { (wchar_t)wch, 0 };
    int n = 1;
    if (wch >= 0xD800 && wch <= 0xDBFF) {
        w[1] = (wchar_t)_getwch();  // the low half of the pair follows
        n = 2;
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, &out[0], need, nullptr, nullptr);
    return out;
}
#endif
void VM::register_console_builtins() {
    // ── Console I/O ──────────────────────────────────────────

    register_native("CLS", 0, 3, [this](const std::vector<Value>& args) -> Value {
        uint8_t r = 0, g = 0, b = 0;
        bool has_color = (args.size() >= 3);
        if (has_color) {
            r = (uint8_t)args[0].to_int();
            g = (uint8_t)args[1].to_int();
            b = (uint8_t)args[2].to_int();
        }
#ifdef GFX
        if (gfx_is_active()) {
            gfx_clear(r, g, b);
            return Value::make_none();
        }
#endif
#ifdef PICOCALC
        // Same rule on the board: with a drawing buffer open, CLS means
        // the picture, not the text console.
        if (picocalc_gfx_buffered()) {
            picocalc_gfx_clear(0);
            return Value::make_none();
        }
#endif
        if (has_color)
            emit("\033[48;2;" + std::to_string((int)r) + ";" + std::to_string((int)g) + ";" + std::to_string((int)b) + "m");
        emit("\033[2J\033[H");
        return Value::make_none();
    });

    register_native("LOCATE", [this](const std::vector<Value>& args) -> Value {
        int row = (int)args[0].to_int();
        int col = (int)args[1].to_int();
        emit("\033[" + std::to_string(row) + ";" + std::to_string(col) + "H");
        return Value::make_none();
    });

    register_native("COLOR", [this](const std::vector<Value>& args) -> Value {
        int fg = (int)args[0].to_int();
        int bg = (args.size() >= 2) ? (int)args[1].to_int() : 0;
#if defined(_WIN32)
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hOut, (WORD)(fg | (bg << 4)));
#else
        // Win console palette → ANSI SGR (same mapping as Console::set_color),
        // so COLOR works in the browser REPL and on POSIX terminals - not just
        // the Windows console.
        auto map = [](int c) { int base = 0; if (c & 1) base |= 1; if (c & 2) base |= 2; if (c & 4) base |= 4; return base; };
        int fg_base = 30 + map(fg & 0x07);
        int bg_base = 40 + map(bg & 0x07);
        bool fg_bright = (fg & 0x08) != 0;
        bool bg_bright = (bg & 0x08) != 0;
        std::string s = fg_bright ? ("\033[1;" + std::to_string(fg_base) + "m")
                                  : ("\033[22;" + std::to_string(fg_base) + "m");
        if (bg == 0) s += "\033[49m";
        else s += "\033[" + std::to_string(bg_bright ? bg_base + 60 : bg_base) + "m";
        emit(s);
#endif
        return Value::make_none();
    });

    register_native("CURSOR", [this](const std::vector<Value>& args) -> Value {
        if (args[0].to_bool())
            emit("\033[?25h");
        else
            emit("\033[?25l");
        return Value::make_none();
    });

    register_native("SLEEP", [this](const std::vector<Value>& args) -> Value {
        int ms = (int)args[0].to_int();
        // Flush stdout so any pending PRINT-with-semicolon output (game frames
        // drawn with LOCATE/COLOR/PRINT but no trailing newline) becomes
        // visible before we go idle. POSIX TTYs are line-buffered by default
        // and would otherwise sit on the buffer until something prints \n.
        std::fflush(stdout);
        // Slice the wait so events get polled while sleeping. Without this,
        // a tight `DO sleep 15 LOOP` could starve key/quit events for many
        // seconds - the periodic on_tick fires only every 2000 VM ticks.
        //
        // The slices are measured against a deadline rather than counted.
        // Counting assumes a five-millisecond slice takes five
        // milliseconds, and on Windows the default timer granularity is
        // about 15.6 ms, so each one took three times its length and
        // SLEEP 1000 waited 3.2 seconds. Against a deadline the loop just
        // runs fewer, longer slices and the error stays inside one tick of
        // whatever the platform's clock happens to be.
        const int slice_ms = 5;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            int chunk = left > slice_ms ? slice_ms : (int)left;
            if (chunk <= 0) chunk = 1;
#ifdef __EMSCRIPTEN__
            // Asyncify: hand control back to the browser so the canvas paints
            // and key/quit events are delivered, then resume here.
            emscripten_sleep(chunk);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
#endif
            if (on_tick) on_tick();
            if (!event_handlers.empty()) event_poll();
#ifdef JDB_MCU
            if (jdb_break_poll()) {
                const CallFrame& bf = frames.back();
                emit("Break at line " + std::to_string(bf.chunk->line_at(bf.ip)) + "\n");
                is_halted = true;
            }
#endif
            if (is_halted) break; // event handler may have run END
        }
        return Value::make_none();
    });

    // YIELD: hand a frame to the host event loop. On the web this lets the
    // browser paint the canvas and deliver input mid-run; on desktop it just
    // pumps ticks/events so a tight loop stays responsive.
    register_native("YIELD", 0, 0, [this](const std::vector<Value>& args) -> Value {
        (void)args;
        std::fflush(stdout);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0);
#endif
        if (on_tick) on_tick();
        if (!event_handlers.empty()) event_poll();
        return Value::make_none();
    });

    register_native("GETX", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO ci;
        if (GetConsoleScreenBufferInfo(h, &ci))
            return Value::make_i64(ci.dwCursorPosition.X + 1);
#endif
        return Value::make_i64(1);
    });

    register_native("GETY", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO ci;
        if (GetConsoleScreenBufferInfo(h, &ci))
            return Value::make_i64(ci.dwCursorPosition.Y + 1);
#endif
        return Value::make_i64(1);
    });

    // INPUT_HIDDEN$([prompt$]) - a line the terminal does not show. The
    // console keeps doing the line editing, so backspace and a pasted
    // password work as they do for INPUT; only the echo is off. Nothing is
    // printed in its place, not even stars: a row of stars tells a shoulder
    // how long the secret is.
    //
    // When stdin is not a terminal there is no echo to turn off and nothing
    // to hide, so a piped line is read as INPUT reads it - which is what
    // lets a test feed one.
    register_native("INPUT_HIDDEN$", 0, 1, [this](const std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            emit(args[0].to_string());
            std::fflush(stdout);
        }
        std::string line;
        is_waiting_input = true;
#if defined(_WIN32)
        // GetConsoleMode fails on a redirected handle, so it is the terminal
        // test as well as the way to change the mode.
        HANDLE in_h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD saved_mode = 0;
        bool echo_off = GetConsoleMode(in_h, &saved_mode) != 0;
        if (echo_off) SetConsoleMode(in_h, saved_mode & ~(DWORD)ENABLE_ECHO_INPUT);
        std::getline(std::cin, line);
        if (echo_off) SetConsoleMode(in_h, saved_mode);
#elif defined(JDB_MCU) || defined(__EMSCRIPTEN__)
        // No terminal to silence on a board or in the browser; reading at all
        // is the best that can be done, and the readme says so.
        std::getline(std::cin, line);
#else
        struct termios saved_tio;
        bool echo_off = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_tio) == 0;
        if (echo_off) {
            struct termios quiet = saved_tio;
            quiet.c_lflag &= ~(tcflag_t)ECHO;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
        }
        std::getline(std::cin, line);
        if (echo_off) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tio);
#endif
        is_waiting_input = false;
        // The Enter was not echoed either, so the cursor is still on the
        // prompt line.
        emit("\n");
        std::fflush(stdout);
        return Value::make_string(line);
    });

    register_native("INKEY$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#ifdef GFX
        extern bool gfx_is_active();
        extern bool gfx_has_key();
        extern std::string gfx_get_key();
        extern void gfx_pump_events();
        if (gfx_is_active()) {
            gfx_pump_events();
            return Value::make_string(gfx_has_key() ? gfx_get_key() : "");
        }
#endif
#ifdef __EMSCRIPTEN__
        { int code = jdb_poll_key_js();
          if (code >= 0) return Value::make_string(std::string(1, (char)(code & 0xFF))); }
        return Value::make_string("");
#endif
#if defined(_WIN32)
        static auto last_check = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check).count() >= 100) {
            last_check = now;
            if (_kbhit()) {
                return Value::make_string(read_console_char());
            }
        }
#endif
        return Value::make_string("");
    });

    register_native("WAITKEY$", 0, -1, [this](const std::vector<Value>& args) -> Value {
        (void)args;
#ifdef GFX
        extern bool gfx_is_active();
        extern bool gfx_has_key();
        extern std::string gfx_get_key();
        extern void gfx_pump_events();
        if (gfx_is_active()) {
            // SDL window has focus, console won't see any keys. Pump SDL
            // events directly so the window stays responsive (otherwise
            // Windows shows the spinning-cursor "not responding" state) and
            // KEYDOWN events get buffered into g_key_available.
            while (true) {
                gfx_pump_events();
                if (gfx_has_key()) return Value::make_string(gfx_get_key());
                if (on_tick) on_tick();
                if (!event_handlers.empty()) event_poll();
                if (is_halted) return Value::make_string("");
#ifdef __EMSCRIPTEN__
                // Must yield to the browser, otherwise it never delivers the
                // canvas keydown events that gfx_pump_events polls - WAITKEY$
                // would busy-loop forever.
                emscripten_sleep(5);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
#endif
            }
        }
#endif
#ifdef __EMSCRIPTEN__
        while (true) {
            int code = jdb_poll_key_js();
            if (code >= 0) return Value::make_string(std::string(1, (char)(code & 0xFF)));
            if (is_halted) return Value::make_string("");
            emscripten_sleep(16);
        }
#endif
#if defined(_WIN32)
        return Value::make_string(read_console_char());
#else
        return Value::make_string("");
#endif
    });

#ifndef JDB_LEAN
    register_native("CLIPBOARD.SET", [](const std::vector<Value>& args) -> Value {
#if defined(_WIN32)
        std::string text = args[0].to_string();
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (hg) { memcpy(GlobalLock(hg), text.c_str(), text.size() + 1); GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); }
            CloseClipboard();
        }
#endif
        return Value::make_none();
    });
#endif

#ifndef JDB_LEAN
    register_native("CLIPBOARD.GET$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(_WIN32)
        if (OpenClipboard(NULL)) {
            HANDLE hg = GetClipboardData(CF_TEXT);
            if (hg) {
                char* s = static_cast<char*>(GlobalLock(hg));
                if (s) { std::string r(s); GlobalUnlock(hg); CloseClipboard(); return Value::make_string(r); }
            }
            CloseClipboard();
        }
#endif
        return Value::make_string("");
    });
#endif

    // ── Diagnostics ──────────────────────────────────────────

    // A diagnostic line reaches the VS Code debug console while a DAP client
    // is attached and stderr otherwise. It never goes to stdout, so a program
    // whose output is redirected still shows its tracing on the terminal.
    auto emit_diagnostic = [this](const std::string& text) {
        if (debug && debug->dap) debug->dap->send_output_message(text + "\n");
        else                     std::cerr << text << std::endl;
    };

    // DEBUG.PRINT v, [v2, ...] - arguments are stringified and joined with a
    // single space.
#ifndef JDB_LEAN
    register_native("DEBUG.PRINT", 1, -1,
                    [emit_diagnostic](const std::vector<Value>& args) -> Value {
        std::string text;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) text += " ";
            text += args[i].to_string();
        }
        emit_diagnostic(text);
        return Value::make_none();
    });
#endif

    // DEBUG.ASSERT cond, [message$] - a std::runtime_error here picks up the
    // native's name and the source line from the CALL handler, which a
    // jdError would bypass.
#ifndef JDB_LEAN
    register_native("DEBUG.ASSERT", 1, 2, [](const std::vector<Value>& args) -> Value {
        if (args[0].to_bool()) return Value::make_none();
        std::string msg = args.size() >= 2 ? args[1].to_string() : std::string();
        throw std::runtime_error(msg.empty() ? "assertion failed"
                                             : "assertion failed: " + msg);
    });
#endif

    // ── Output capture ─────────────────────────────────────────
    // Redirects PRINT/EMIT output to a per-capture string buffer so that
    // tools like the MCP server can return what a snippet printed instead
    // of letting it leak to stdout. Stacked: each BEGIN saves the previous
    // on_output handler (Console's workspace router etc.) and END$ restores it.

#ifndef JDB_LEAN
    register_native("OUTPUT.CAPTURE_BEGIN", 0, 0,
        [this](const std::vector<Value>& args) -> Value {
        (void)args;
        auto buf = std::make_shared<std::string>();
        output_capture_buffers.push_back(buf);
        output_capture_prev.push_back(on_output);
        on_output = [buf](const std::string& s) { buf->append(s); };
        return Value::make_none();
    });
#endif

#ifndef JDB_LEAN
    register_native("OUTPUT.CAPTURE_END$", 0, 0,
        [this](const std::vector<Value>& args) -> Value {
        (void)args;
        if (output_capture_buffers.empty())
            throw std::runtime_error("OUTPUT.CAPTURE_END$: no capture is active");
        auto buf = output_capture_buffers.back();
        output_capture_buffers.pop_back();
        on_output = output_capture_prev.back();
        output_capture_prev.pop_back();
        return Value::make_string(*buf);
    });
#endif

#ifndef JDB_LEAN
    register_native("OUTPUT.CAPTURE_PEEK$", 0, 0,
        [this](const std::vector<Value>& args) -> Value {
        (void)args;
        if (output_capture_buffers.empty())
            throw std::runtime_error("OUTPUT.CAPTURE_PEEK$: no capture is active");
        return Value::make_string(*output_capture_buffers.back());
    });
#endif
}
