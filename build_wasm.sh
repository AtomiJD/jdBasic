#!/usr/bin/env bash
# WASM (Emscripten) build of jdBasic for the browser IDE.
# Runtime: core interpreter + GFX (SDL3) + ImGui + SQLite. No HTTP/LLVM/FFI/
# threads (browser is single-threaded; blocking is handled via Asyncify).
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
DEF="-DGFX -DIMGUI -DSQLITE"
OPT="${OPT:--O0}"
CXXFLAGS="-std=c++17 $OPT $DEF $INC $PORTS"

CORE="src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
      src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
      src/gui.cpp src/ai.cpp src/llm.cpp src/channels.cpp src/file_streams.cpp \
      src/numerics.cpp src/screencap.cpp src/pybridge.cpp"
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

# Stubs for the satellite libs that have no Emscripten port yet (SDL3_image,
# SDL3_mixer). Plain C, so compile separately (no -std=c++17). Replaced by real
# emcc-built libs in the "nachziehen" phase.
STUB_OBJ=""
if [ -f src/wasm_av_stub.c ]; then
    emcc -O2 $INC $PORTS -c src/wasm_av_stub.c -o "$OUT/wasm_av_stub.o"
    STUB_OBJ="$OUT/wasm_av_stub.o"
fi

EMFLAGS="-sWASM=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=1 -sEXIT_RUNTIME=0 \
         -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
         ${LINKEXTRA:-}"

emcc $CXXFLAGS $EMFLAGS \
  $CORE $GFX $IMGUI $SQL $ENTRY "$OUT/sqlite3.o" $STUB_OBJ \
  -o "$OUT/index.html"
echo "BUILD OK: $OUT/index.{html,js,wasm}"
