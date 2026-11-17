#!/usr/bin/env bash
# WASM (Emscripten) build of jdBasic for the browser IDE.
# Runtime: core interpreter + GFX (SDL3, SDL3_image) + ImGui + SQLite + FX. No LLVM/FFI/
# threads (browser is single-threaded; blocking is handled via Asyncify).
# HTTP.GET$/POST$ come from wasm_net.cpp over fetch, not httplib.
# FX is WAV.READ/WRITE/INFO plus the effects chain - audio_fx.cpp is plain
# standard C++ with no device or platform API, so it ports as-is and works
# against MEMFS. The device-bound flags stay out: MINIAUDIO (MON.*, capture),
# MIDI (RtMidi needs a platform backend) and FORMS (Win32).
# Run on cortex from ~/cc_wasm/cc with emsdk sourced.
set -e
cd "$(dirname "$0")"
source ~/cc_wasm/emsdk/emsdk_env.sh >/dev/null 2>&1

OUT=build_wasm
mkdir -p "$OUT"

PORTS="--use-port=sdl3 --use-port=sdl3_ttf"
INC="-Isrc -Ilibs/eigen -Ilibs/imgui -Ilibs/imgui/backends \
     -Ilibs/SDL3_image/include -Ilibs/SDL3_mixer/include/SDL3_mixer \
     -Ibridges/sqlitebridge"
DEF="-DGFX -DIMGUI -DSQLITE -DFX"
# -O2: the interpreter dispatch + APL array ops are hot; -O0 makes compute
# demos (universe, mandelbrot, ...) crawl in the browser. -O2 is the sweet
# spot (much faster than -O0, smaller .wasm than -O3).
OPT="${OPT:--O2}"
# -fexceptions: the jdBasic runtime throws on errors (bad SAVE, type errors, ...).
# Without it a throw aborts the whole module; with it our jdb_run try/catch
# reports the error and the REPL keeps running.
CXXFLAGS="-std=c++17 $OPT $DEF $INC $PORTS -fexceptions"

CORE="src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp src/vm_builtins_math.cpp src/vm_builtins_string.cpp src/vm_builtins_data.cpp src/vm_builtins_array.cpp src/vm_builtins_datetime.cpp src/vm_builtins_codec.cpp src/vm_builtins_console.cpp src/vm_builtins_file.cpp src/vm_builtins_system.cpp src/vm_builtins_async.cpp \
      src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
      src/audio_fx.cpp src/audio_io.cpp src/midi.cpp \
      src/gui.cpp src/ai.cpp src/llm.cpp src/channels.cpp src/file_streams.cpp \
      src/numerics.cpp src/screencap.cpp src/pybridge.cpp src/wasm_net.cpp"
GFX="src/graphics.cpp src/sprites.cpp src/tiledmap.cpp"
IMGUI="libs/imgui/imgui.cpp libs/imgui/imgui_draw.cpp \
       libs/imgui/imgui_tables.cpp libs/imgui/imgui_widgets.cpp \
       libs/imgui/backends/imgui_impl_sdl3.cpp \
       libs/imgui/backends/imgui_impl_sdlrenderer3.cpp"
SQL="src/sql.cpp"
# wasm_main.cpp replaces the desktop console main once it exists.
ENTRY="src/main.cpp"

# SQLite amalgamation: plain C, compile once.
[ -f "$OUT/sqlite3.o" ] || emcc -O2 -c bridges/sqlitebridge/sqlite3.c -o "$OUT/sqlite3.o"

# SDL3_image has no Emscripten port, so its sources from libs/SDL3_image are
# compiled here as plain C. USE_STBIMAGE decodes PNG and JPG through the
# bundled stb_image.h (no libpng / libjpeg needed); PNG and JPG saving use the
# bundled miniz.h and tiny_jpeg.h. The other LOAD_ formats are self-contained.
# Every IMG_*.c is compiled because IMG.c references each format's IMG_is*
# and IMG_Load*_IO; formats without a LOAD_ define compile to stubs. The
# platform backends (IMG_WIC.c, IMG_ImageIO.m) are left out.
IMG_SRC=libs/SDL3_image/src
IMG_OBJ=""
if [ -f "$IMG_SRC/IMG.c" ]; then
    IMG_DEF="-DUSE_STBIMAGE -DLOAD_PNG -DLOAD_JPG -DLOAD_BMP -DLOAD_GIF \
             -DLOAD_TGA -DLOAD_QOI -DLOAD_PNM -DLOAD_PCX -DLOAD_LBM \
             -DLOAD_XPM -DLOAD_XV -DLOAD_XCF -DLOAD_SVG"
    mkdir -p "$OUT/sdl3_image"
    for f in IMG IMG_avif IMG_bmp IMG_gif IMG_jpg IMG_jxl IMG_lbm IMG_pcx \
             IMG_png IMG_pnm IMG_qoi IMG_stb IMG_svg IMG_tga IMG_tif \
             IMG_webp IMG_xcf IMG_xpm IMG_xv IMG_xxx; do
        o="$OUT/sdl3_image/$f.o"
        [ "$o" -nt "$IMG_SRC/$f.c" ] || \
            emcc -O2 -Ilibs/SDL3_image/include $IMG_DEF $PORTS -c "$IMG_SRC/$f.c" -o "$o"
        IMG_OBJ="$IMG_OBJ $o"
    done
fi

# Stubs for SDL3_mixer, which has no Emscripten port yet, and for SDL3_image
# when libs/SDL3_image is missing (then image loads fail with an error that
# says so). Plain C, so compile separately (no -std=c++17).
STUB_OBJ=""
if [ -f src/wasm_av_stub.c ]; then
    STUB_DEF=""
    [ -n "$IMG_OBJ" ] && STUB_DEF="-DJDB_WASM_SDL3_IMAGE"
    emcc -O2 $INC $PORTS $STUB_DEF -c src/wasm_av_stub.c -o "$OUT/wasm_av_stub.o"
    STUB_OBJ="$OUT/wasm_av_stub.o"
fi

# Demo programs (LOAD/RUN from the in-browser MEMFS), embedded at the FS root.
DEMOS="jdb/demos/apl/oneliners.jdb jdb/demos/turtle/turtle_fib.jdb \
       jdb/demos/turtle/turtle_tree.jdb jdb/demos/games/minesweeper.jdb \
       jdb/demos/games/tetris_game.jdb jdb/demos/games/snake_game.jdb \
       jdb/demos/graphics/universe.jdb \
       jdb/demos/graphics/graph_demo.jdb"
EMBED=""
for d in $DEMOS; do
    [ -f "$d" ] && EMBED="$EMBED --embed-file $d@/$(basename "$d")"
done
# Default TTF for graphics TEXT/SETFONT, embedded at the FS root where the
# WASM build of try_load_default_font looks for it.
[ -f jdbasic_default.ttf ] && EMBED="$EMBED --embed-file jdbasic_default.ttf@/jdbasic_default.ttf"
# help.txt at the FS root so the HELP command finds it (load_help_file tries
# "help.txt" relative to the MEMFS cwd, which is /).
[ -f help.txt ] && EMBED="$EMBED --embed-file help.txt@/help.txt"
# Web-specific programs (e.g. graphics tuned with YIELD) live under wasm/programs,
# embedded at the FS root; a subfolder keeps its name (wasm/programs/lessons =
# the Train jdBasic lesson samples -> /lessons).
if [ -d wasm/programs ]; then
    for d in wasm/programs/*.jdb; do
        [ -f "$d" ] && EMBED="$EMBED --embed-file $d@/$(basename "$d")"
    done
    for d in wasm/programs/*/*.jdb; do
        [ -f "$d" ] && EMBED="$EMBED --embed-file $d@/$(basename "$(dirname "$d")")/$(basename "$d")"
    done
fi
# The songs of When Kaiku Dreams Techno with the SCORE and STAGE modules they
# import, and the CLI module STAGE imports -> /music; IMPORT finds the
# modules beside the song.
for d in jdb/demos/sound/score.jdb jdb/demos/sound/stage.jdb lib/cli.jdb \
         jdb/demos/sound/synthwave_highway.jdb jdb/demos/sound/disco_glitter.jdb \
         jdb/demos/sound/house_rooftop.jdb jdb/demos/sound/acid_copper.jdb \
         jdb/demos/sound/trance_live.jdb jdb/demos/sound/dnb_nightbus.jdb; do
    [ -f "$d" ] && EMBED="$EMBED --embed-file $d@/music/$(basename "$d")"
done

# ASYNCIFY_STACK_SIZE: the default 4 KB is too small for our deeply-nested
# bytecode interpreter - a suspend (INPUT, SLEEP, SCREENFLIP, YIELD) deep in
# the dispatch loop overflows it and aborts with "unreachable". 1 MB is ample.
# STACK_SIZE: emscripten's 64 KB default C stack overflows on large programs
# with deep SUB/expression nesting (e.g. space_shooter), surfacing as
# "memory access out of bounds". 16 MB matches a roomy desktop stack.
EMFLAGS="-sWASM=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576 -sSTACK_SIZE=16777216 -sEXIT_RUNTIME=0 \
         -fexceptions \
         -sENVIRONMENT=web,node \
         -sMODULARIZE=1 -sEXPORT_NAME=createJdBasic \
         -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','stringToUTF8','lengthBytesUTF8'] \
         -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
         $EMBED ${LINKEXTRA:-}"

# Stamp the build number (from build_number.txt, same source as build.bat) and
# date into the banner. Quoted here so the macro expands to a string literal.
BNUM=0
[ -f build_number.txt ] && BNUM=$(tr -d '\r\n' < build_number.txt)
BDATE=$(date +%Y-%m-%d)

emcc $CXXFLAGS $EMFLAGS \
  -DJDBASIC_BUILD_NUM="\"$BNUM\"" -DJDBASIC_BUILD_DATE="\"$BDATE\"" \
  $CORE $GFX $IMGUI $SQL $ENTRY "$OUT/sqlite3.o" $IMG_OBJ $STUB_OBJ \
  -o "$OUT/jdbasic.js"
echo "BUILD OK: $OUT/jdbasic.{js,wasm} (Build $BNUM, $BDATE, factory: createJdBasic)"
