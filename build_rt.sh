#!/usr/bin/env bash
# Build the jdBasic runtime/embed shared library: build/libjdbrt.so.
#
# This is the Linux/macOS parallel to build_rt.bat. The .so exports two
# C-ABIs:
#   jdrt_*       - runtime bridge consumed by native-compiled (-c) exes
#   jdb_embed_*  - host-app API consumed by embedders (Godot GDExtension)
#
# It is headless by default - no GFX / IMGUI - which is the correct flavour
# for an embed host (Godot brings its own renderer; pulling SDL3/ImGui in
# would clash). Toggle modules with env vars:
#   LLM=1 SOUND=1 HTTP=1 ./build_rt.sh   (recommended for the Godot embed)
#   GFX=1 IMGUI=1 ./build_rt.sh          (only for a graphical native runtime)
# Override compiler/jobs: CXX=clang++ JOBS=4 ./build_rt.sh
set -e
mkdir -p build build/obj_pic

default_jobs() {
    if command -v nproc >/dev/null 2>&1; then nproc
    elif command -v sysctl >/dev/null 2>&1; then sysctl -n hw.ncpu
    else echo 4
    fi
}
if [ -z "${CXX:-}" ]; then
    case "$(uname -s)" in
        Darwin) CXX=clang++ ;;
        *)      CXX=g++ ;;
    esac
fi
JOBS=${JOBS:-$(default_jobs)}
CXXFLAGS="-std=c++17 -O2 -DNDEBUG -fPIC -DJDRT_EXPORTS -Isrc -Ilibs/eigen"
LDFLAGS="-shared -ldl -lpthread -lm"

# Homebrew (freetype, harfbuzz, libpng, openssl etc.) lives under brew's prefix
# on macOS and needs explicit -I/-L paths. Mirrors build.sh.
BREW_PREFIX=""
if [ "$(uname -s)" = "Darwin" ]; then
    if command -v brew >/dev/null 2>&1; then BREW_PREFIX="$(brew --prefix)"
    elif [ -d /opt/homebrew ]; then BREW_PREFIX=/opt/homebrew
    elif [ -d /usr/local/Homebrew ]; then BREW_PREFIX=/usr/local
    fi
    if [ -n "$BREW_PREFIX" ]; then
        CXXFLAGS="$CXXFLAGS -I$BREW_PREFIX/include -I$BREW_PREFIX/opt/openssl@3/include"
        LDFLAGS="$LDFLAGS -L$BREW_PREFIX/lib -L$BREW_PREFIX/opt/openssl@3/lib"
    fi
fi

WANT_HTTP=${HTTP:-0}
WANT_GFX=${GFX:-0}
WANT_IMGUI=${IMGUI:-0}
WANT_LLM=${LLM:-0}
WANT_SOUND=${SOUND:-0}
WANT_SQLITE=${SQLITE:-0}

# Base translation units - mirror build_rt.bat's always-on list. gui.cpp and
# sound.cpp guard their device code behind #ifdef GFX / SOUND_DSP, so they
# compile in a headless build too.
SRC="src/vm_bridge.cpp src/vm.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp \
     src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
     src/gui.cpp src/ai.cpp src/llm.cpp src/channels.cpp src/file_streams.cpp \
     src/jdb_embed_api.cpp src/numerics.cpp src/screencap.cpp"

if [ "$WANT_HTTP" = "1" ]; then
    CXXFLAGS="$CXXFLAGS -DHTTP -DCPPHTTPLIB_OPENSSL_SUPPORT"
    LDFLAGS="$LDFLAGS -lssl -lcrypto"
    SRC="$SRC src/http.cpp"
fi

if [ "$WANT_SOUND" = "1" ]; then
    # Sequencer DSP in pull mode - no audio device; the host pulls samples.
    CXXFLAGS="$CXXFLAGS -DSOUND_DSP"
fi

if [ "$WANT_GFX" = "1" ]; then
    SDL3_DIR="libs/SDL3"; SDL3_TTF_DIR="libs/SDL3_ttf"
    SDL3_IMG_DIR="libs/SDL3_image"; SDL3_MIX_DIR="libs/SDL3_mixer"
    for d in "$SDL3_DIR/build/libSDL3.a" "$SDL3_TTF_DIR/build/libSDL3_ttf.a" \
             "$SDL3_IMG_DIR/build/libSDL3_image.a" "$SDL3_MIX_DIR/build/libSDL3_mixer.a"; do
        [ -f "$d" ] || { echo "ERROR: $d missing - run ./build_libs.sh first"; exit 1; }
    done
    CXXFLAGS="$CXXFLAGS -DGFX -I$SDL3_DIR/include -I$SDL3_TTF_DIR/include \
        -I$SDL3_IMG_DIR/include -I$SDL3_MIX_DIR/include/SDL3_mixer"
    LDFLAGS="$LDFLAGS $SDL3_IMG_DIR/build/libSDL3_image.a $SDL3_TTF_DIR/build/libSDL3_ttf.a \
        $SDL3_MIX_DIR/build/libSDL3_mixer.a $SDL3_DIR/build/libSDL3.a \
        -lfreetype -lharfbuzz -lpng -ljpeg -ltiff -lwebp -lwebpdemux"
    if [ "$(uname -s)" = "Darwin" ]; then
        LDFLAGS="$LDFLAGS \
            -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework CoreAudio -framework AudioToolbox -framework Carbon \
            -framework ForceFeedback -framework Metal -framework MetalKit \
            -framework GameController -framework CoreHaptics \
            -framework CoreFoundation -framework UniformTypeIdentifiers \
            -framework AVFoundation -framework CoreMedia \
            -framework QuartzCore"
    fi
    # Linux: OS.SCREENSHOT (src/screencap.cpp) calls Xlib directly.
    if [ "$(uname -s)" = "Linux" ]; then
        LDFLAGS="$LDFLAGS -lX11"
    fi
    SRC="$SRC src/graphics.cpp src/sprites.cpp src/tiledmap.cpp"
    if [ "$WANT_IMGUI" = "1" ]; then
        IMGUI_LIB="libs/imgui"
        CXXFLAGS="$CXXFLAGS -DIMGUI -I$IMGUI_LIB -I$IMGUI_LIB/backends"
        SRC="$SRC $IMGUI_LIB/imgui.cpp $IMGUI_LIB/imgui_draw.cpp \
             $IMGUI_LIB/imgui_tables.cpp $IMGUI_LIB/imgui_widgets.cpp \
             $IMGUI_LIB/backends/imgui_impl_sdl3.cpp \
             $IMGUI_LIB/backends/imgui_impl_sdlrenderer3.cpp"
    fi
elif [ "$WANT_IMGUI" = "1" ]; then
    echo "ERROR: IMGUI=1 requires GFX=1"; exit 1
fi

if [ "$WANT_LLM" = "1" ]; then
    if [ "${LLM_SYSTEM:-0}" = "1" ]; then
        # System-installed llama.cpp - e.g. inside the Strix-Halo vulkan
        # distrobox where /lib64 has libllama.so + libggml-vulkan.so wired to
        # the Radeon iGPU. Build (and run Godot) inside that container. ggml's
        # backend split varies by build (base/cpu/vulkan/cuda), so link only
        # the ggml-* libs that are actually installed.
        CXXFLAGS="$CXXFLAGS -DLLM"
        LLM_LIBS="-lllama -lggml"
        for b in ggml-base ggml-cpu ggml-vulkan ggml-cuda; do
            if ldconfig -p 2>/dev/null | grep -q "lib$b\.so"; then LLM_LIBS="$LLM_LIBS -l$b"; fi
        done
        LDFLAGS="$LDFLAGS $LLM_LIBS"
    else
        LLAMA_DIR="libs/llama"
        for a in libllama.a libggml.a libggml-base.a libggml-cpu.a; do
            [ -f "$LLAMA_DIR/$a" ] || { echo "ERROR: $LLAMA_DIR/$a missing - run ./build_libs.sh first (or set LLM_SYSTEM=1)"; exit 1; }
        done
        CXXFLAGS="$CXXFLAGS -DLLM -I$LLAMA_DIR"
        LLM_LIBS="$LLAMA_DIR/libllama.a $LLAMA_DIR/libggml.a $LLAMA_DIR/libggml-cpu.a $LLAMA_DIR/libggml-base.a"
        if [ -f "$LLAMA_DIR/libggml-cuda.a" ]; then
            LLM_LIBS="$LLM_LIBS $LLAMA_DIR/libggml-cuda.a -lcudart -lcublas -lcublasLt -lcuda"
        fi
        # Static archives go AFTER the objects on the link line.
        LDFLAGS="$LDFLAGS $LLM_LIBS"
    fi
fi

if [ "$WANT_SQLITE" = "1" ]; then
    if [ ! -f bridges/sqlitebridge/sqlite3.c ]; then
        echo "ERROR: SQLITE needs the amalgamation - download sqlite3.c/sqlite3.h"
        echo "       from https://sqlite.org/download.html into bridges/sqlitebridge/"
        exit 1
    fi
    CXXFLAGS="$CXXFLAGS -DSQLITE -Ibridges/sqlitebridge"
    SRC="$SRC src/sql.cpp"
    mkdir -p build/obj_pic
    if [ ! -f build/obj_pic/sqlite3.o ]; then
        echo "[+] Compiling SQLite amalgamation one-time (PIC)..."
        cc -O2 -fPIC -c bridges/sqlitebridge/sqlite3.c -o build/obj_pic/sqlite3.o
    fi
    LDFLAGS="$LDFLAGS build/obj_pic/sqlite3.o -lpthread -ldl -lm"
fi

# ── Parallel compile to build/obj_pic, then link the .so ─────────
FLAGS_HASH=$(echo "$CXX $CXXFLAGS" | sha1sum | cut -c1-12)
STAMP="build/obj_pic/.rtflags-$FLAGS_HASH"
if [ ! -f "$STAMP" ]; then
    rm -f build/obj_pic/.rtflags-* 2>/dev/null
    # sqlite3.o is a C object built with `cc` (independent of CXX/CXXFLAGS) and
    # compiled one-time earlier; deleting it here breaks the first SQLITE .so build.
    find build/obj_pic -maxdepth 1 -name '*.o' ! -name 'sqlite3.o' -delete 2>/dev/null
    touch "$STAMP"
fi

OBJS=()
TO_BUILD=()
for s in $SRC; do
    o="build/obj_pic/$(basename "$s" .cpp).o"; OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then TO_BUILD+=("$s|$o"); fi
done

features="embed"
[ "$WANT_HTTP"  = "1" ] && features="$features+HTTP"
[ "$WANT_SOUND" = "1" ] && features="$features+SOUND"
[ "$WANT_LLM"   = "1" ] && features="$features+LLM"
[ "$WANT_GFX"   = "1" ] && features="$features+GFX"
[ "$WANT_IMGUI" = "1" ] && features="$features+IMGUI"
[ "$WANT_SQLITE" = "1" ] && features="$features+SQLITE"
echo "== Building libjdbrt.so ($features) - $JOBS jobs, ${#TO_BUILD[@]} of ${#OBJS[@]} stale =="

export CXX CXXFLAGS
if [ "${#TO_BUILD[@]}" -gt 0 ]; then
    printf '%s\n' "${TO_BUILD[@]}" | \
        xargs -P "$JOBS" -n1 bash -c '
            line="$0"; src="${line%%|*}"; obj="${line##*|}"
            echo "  CC (PIC) $src"
            $CXX $CXXFLAGS -c "$src" -o "$obj"
        '
fi

echo "== Linking =="
$CXX -o build/libjdbrt.so "${OBJS[@]}" $LDFLAGS
echo "OK: build/libjdbrt.so"
if command -v nm >/dev/null 2>&1; then
    echo "   exports: $(nm -D --defined-only build/libjdbrt.so 2>/dev/null | grep -c jdb_embed_) jdb_embed_*, $(nm -D --defined-only build/libjdbrt.so 2>/dev/null | grep -c jdrt_) jdrt_*"
fi
