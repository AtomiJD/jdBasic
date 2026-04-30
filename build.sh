#!/usr/bin/env bash
# Parallel-compile build script for jdBasic on Linux.
# Compiles each source to build/obj/<basename>.o in parallel (one job per
# core) with stamp-file-based incremental rebuilds, then links once.
#
# Toggle modules: HTTP=0, GFX=0, IMGUI=1 (default IMGUI=0).
# Override compiler: CXX=clang++ ./build.sh
# Override jobs:    JOBS=4 ./build.sh
set -e
mkdir -p build build/obj

CXX=${CXX:-g++}
JOBS=${JOBS:-$(nproc)}
CXXFLAGS="-std=c++17 -O2 -DNDEBUG -Isrc"
LDFLAGS="-ldl -lpthread"

WANT_HTTP=${HTTP:-1}
WANT_GFX=${GFX:-1}
WANT_IMGUI=${IMGUI:-0}

if [ "$WANT_HTTP" = "1" ]; then
    CXXFLAGS="$CXXFLAGS -DHTTP -DCPPHTTPLIB_OPENSSL_SUPPORT"
    LDFLAGS="$LDFLAGS -lssl -lcrypto"
    HTTP_SRC="src/http.cpp"
else
    HTTP_SRC=""
fi

GFX_SRC=""
if [ "$WANT_GFX" = "1" ]; then
    SDL3_DIR="libs/SDL3"
    SDL3_TTF_DIR="libs/SDL3_ttf"
    SDL3_IMG_DIR="libs/SDL3_image"
    SDL3_MIX_DIR="libs/SDL3_mixer"
    for d in "$SDL3_DIR/build/libSDL3.a" "$SDL3_TTF_DIR/build/libSDL3_ttf.a" \
             "$SDL3_IMG_DIR/build/libSDL3_image.a" "$SDL3_MIX_DIR/build/libSDL3_mixer.a"; do
        if [ ! -f "$d" ]; then
            echo "ERROR: $d missing — run ./build_libs.sh first"; exit 1
        fi
    done
    CXXFLAGS="$CXXFLAGS -DGFX \
        -I$SDL3_DIR/include \
        -I$SDL3_TTF_DIR/include \
        -I$SDL3_IMG_DIR/include \
        -I$SDL3_MIX_DIR/include/SDL3_mixer"
    LDFLAGS="$LDFLAGS \
        $SDL3_IMG_DIR/build/libSDL3_image.a \
        $SDL3_TTF_DIR/build/libSDL3_ttf.a \
        $SDL3_MIX_DIR/build/libSDL3_mixer.a \
        $SDL3_DIR/build/libSDL3.a \
        -lfreetype -lharfbuzz -lpng -ljpeg -ltiff -lwebp -lwebpdemux \
        -lm"
    GFX_SRC="src/graphics.cpp src/tiledmap.cpp"
fi

IMGUI_SRC=""
if [ "$WANT_IMGUI" = "1" ]; then
    if [ "$WANT_GFX" != "1" ]; then
        echo "ERROR: IMGUI=1 requires GFX=1"; exit 1
    fi
    IMGUI_LIB="libs/imgui"
    if [ ! -f "$IMGUI_LIB/imgui.cpp" ]; then
        echo "ERROR: $IMGUI_LIB missing"; exit 1
    fi
    CXXFLAGS="$CXXFLAGS -DIMGUI \
        -I$IMGUI_LIB \
        -I$IMGUI_LIB/backends"
    IMGUI_SRC="$IMGUI_LIB/imgui.cpp $IMGUI_LIB/imgui_draw.cpp \
               $IMGUI_LIB/imgui_tables.cpp $IMGUI_LIB/imgui_widgets.cpp \
               $IMGUI_LIB/backends/imgui_impl_sdl3.cpp \
               $IMGUI_LIB/backends/imgui_impl_sdlrenderer3.cpp"
fi

SRC="src/main.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
     src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
     src/gui.cpp src/ai.cpp src/llm.cpp $HTTP_SRC $GFX_SRC $IMGUI_SRC"

# ── Compile in parallel ──────────────────────────────────────
# Map src/foo.cpp → build/obj/foo.o, libs/imgui/imgui.cpp → build/obj/imgui.o.
# We hash the flags into a stamp file so a flag change forces a rebuild.

obj_path() {
    echo "build/obj/$(basename "$1" .cpp).o"
}

FLAGS_HASH=$(echo "$CXX $CXXFLAGS" | sha1sum | cut -c1-12)
STAMP="build/obj/.flags-$FLAGS_HASH"
if [ ! -f "$STAMP" ]; then
    rm -f build/obj/.flags-* 2>/dev/null
    rm -f build/obj/*.o 2>/dev/null
    touch "$STAMP"
fi

OBJS=()
TO_BUILD=()
for s in $SRC; do
    o=$(obj_path "$s"); OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
        TO_BUILD+=("$s|$o")
    fi
done

features="console"
[ "$WANT_HTTP"  = "1" ] && features="$features+HTTP"
[ "$WANT_GFX"   = "1" ] && features="$features+GFX"
[ "$WANT_IMGUI" = "1" ] && features="$features+IMGUI"
echo "== Building jdBasic ($features) — $JOBS jobs, ${#TO_BUILD[@]} of ${#OBJS[@]} stale =="

# xargs -P parallelises; each line is "src|obj"
if [ "${#TO_BUILD[@]}" -gt 0 ]; then
    printf '%s\n' "${TO_BUILD[@]}" | \
        xargs -P "$JOBS" -I{} bash -c '
            line="{}"; src="${line%%|*}"; obj="${line##*|}"
            echo "  CC $src"
            '"$CXX"' '"$CXXFLAGS"' -c "$src" -o "$obj"
        '
fi

echo "== Linking =="
$CXX "${OBJS[@]}" -o build/jdbasic $LDFLAGS
echo "OK: build/jdbasic"
