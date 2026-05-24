#!/usr/bin/env bash
# Parallel-compile build script for jdBasic on Linux.
# Compiles each source to build/obj/<basename>.o in parallel (one job per
# core) with stamp-file-based incremental rebuilds, then links once.
#
# Toggle modules: HTTP=0, GFX=0, IMGUI=1, MCPSERVER=1 (default off).
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
WANT_LLM=${LLM:-0}
WANT_ONNX=${ONNX:-0}
WANT_NATIVEC=${NATIVEC:-0}
WANT_MCPSERVER=${MCPSERVER:-0}
WANT_FTXUI=${FTXUI:-0}
WANT_TUI=${TUI:-0}
# TUI implies FTXUI — drag the lib in if only TUI was passed.
if [ "$WANT_TUI" = "1" ]; then WANT_FTXUI=1; fi

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
    GFX_SRC="src/graphics.cpp src/sprites.cpp src/tiledmap.cpp"
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

if [ "$WANT_LLM" = "1" ]; then
    if [ "${LLM_SYSTEM:-0}" = "1" ]; then
        # System-installed llama.cpp — e.g., when building inside the
        # Strix-Halo distrobox where /usr/lib64 already has libllama.so
        # + libggml-vulkan.so wired up against the Radeon iGPU.
        CXXFLAGS="$CXXFLAGS -DLLM"
        LDFLAGS="$LDFLAGS -lllama -lggml -lggml-cpu -lggml-base"
    else
        LLAMA_DIR="libs/llama"
        for a in libllama.a libggml.a libggml-base.a libggml-cpu.a; do
            if [ ! -f "$LLAMA_DIR/$a" ]; then
                echo "ERROR: $LLAMA_DIR/$a missing — run ./build_libs.sh first (or set LLM_SYSTEM=1 if libs are at /usr)"; exit 1
            fi
        done
        CXXFLAGS="$CXXFLAGS -DLLM -I$LLAMA_DIR"
        # Order: llama links against ggml, ggml-cpu against ggml-base.
        LDFLAGS="$LDFLAGS \
            $LLAMA_DIR/libllama.a \
            $LLAMA_DIR/libggml.a \
            $LLAMA_DIR/libggml-cpu.a \
            $LLAMA_DIR/libggml-base.a"
        if [ -f "$LLAMA_DIR/libggml-cuda.a" ]; then
            LDFLAGS="$LDFLAGS $LLAMA_DIR/libggml-cuda.a -lcudart -lcublas -lcublasLt -lcuda"
        fi
    fi
fi

if [ "$WANT_ONNX" = "1" ]; then
    ORT_DIR="libs/onnxruntime"
    if [ ! -f "$ORT_DIR/lib/libonnxruntime.so" ]; then
        echo "ERROR: $ORT_DIR/lib/libonnxruntime.so missing — fetch the prebuilt tarball"; exit 1
    fi
    CXXFLAGS="$CXXFLAGS -DONNX -I$ORT_DIR/include"
    # Dynamic-link onnxruntime; rpath \$ORIGIN/../libs/onnxruntime/lib so
    # the binary finds it when run from the project root or build/.
    LDFLAGS="$LDFLAGS -L$ORT_DIR/lib -lonnxruntime \
        -Wl,-rpath,\$ORIGIN/../$ORT_DIR/lib \
        -Wl,-rpath,\$ORIGIN/$ORT_DIR/lib"
fi

MCPSERVER_SRC=""
if [ "$WANT_MCPSERVER" = "1" ]; then
    CXXFLAGS="$CXXFLAGS -DMCPSERVER"
    MCPSERVER_SRC="src/mcp_stdio.cpp"
fi
FTXUI_SRC=""
if [ "$WANT_FTXUI" = "1" ]; then
    FTXUI_DIR="libs/ftxui"
    for a in libftxui-component.a libftxui-dom.a libftxui-screen.a; do
        if [ ! -f "$FTXUI_DIR/build/$a" ]; then
            echo "ERROR: $FTXUI_DIR/build/$a missing — build ftxui first:"
            echo "  cd libs/ftxui && mkdir -p build && cd build && \\"
            echo "    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\"
            echo "          -DFTXUI_BUILD_EXAMPLES=OFF -DFTXUI_BUILD_TESTS=OFF -DFTXUI_BUILD_DOCS=OFF .. && \\"
            echo "    cmake --build . -j"
            exit 1
        fi
    done
    CXXFLAGS="$CXXFLAGS -DFTXUI -I$FTXUI_DIR/include"
    # Order matters: component → dom → screen (high-level first).
    LDFLAGS="$LDFLAGS \
        $FTXUI_DIR/build/libftxui-component.a \
        $FTXUI_DIR/build/libftxui-dom.a \
        $FTXUI_DIR/build/libftxui-screen.a"
    FTXUI_SRC="src/repl_ftxui.cpp"
fi

TUI_SRC=""
if [ "$WANT_TUI" = "1" ]; then
    CXXFLAGS="$CXXFLAGS -DTUI"
    TUI_SRC="src/tui.cpp src/tui_state.cpp"
fi

NATIVEC_SRC=""
if [ "$WANT_NATIVEC" = "1" ]; then
    LLVM_CONFIG=${LLVM_CONFIG:-llvm-config-18}
    if ! command -v "$LLVM_CONFIG" >/dev/null 2>&1; then
        echo "ERROR: $LLVM_CONFIG not found — install llvm-18-dev or set LLVM_CONFIG="; exit 1
    fi
    LLVM_INC=$($LLVM_CONFIG --includedir)
    CXXFLAGS="$CXXFLAGS -DLLVM_CODEGEN -I$LLVM_INC"
    # Dynamic LLVM (libLLVM-18.so). Avoids 200+ MB of static archives.
    LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -lLLVM-18"
    NATIVEC_SRC="src/llvm_codegen.cpp"
fi

SRC="src/main.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
     src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
     src/gui.cpp src/ai.cpp src/llm.cpp src/channels.cpp src/file_streams.cpp \
     $HTTP_SRC $GFX_SRC $IMGUI_SRC $NATIVEC_SRC $MCPSERVER_SRC $FTXUI_SRC $TUI_SRC"

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
[ "$WANT_LLM"     = "1" ] && features="$features+LLM"
[ "$WANT_ONNX"    = "1" ] && features="$features+ONNX"
[ "$WANT_NATIVEC" = "1" ] && features="$features+NATIVEC"
[ "$WANT_MCPSERVER" = "1" ] && features="$features+MCPSERVER"
[ "$WANT_FTXUI" = "1" ] && features="$features+FTXUI"
[ "$WANT_TUI" = "1" ] && features="$features+TUI"
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

# When NATIVEC is on, build the runtime support pieces too:
#  - build/jdb_runtime.o: small static obj statically linked into every
#    generated exe (basic intrinsics — printf, math, time, etc.)
#  - build/libjdbrt.so:   shared library providing the full VM via the
#    jdrt_* C-API, dlopen-style dependency of every generated exe
if [ "$WANT_NATIVEC" = "1" ]; then
    if [ ! -f build/jdb_runtime.o ] || [ src/jdb_runtime.cpp -nt build/jdb_runtime.o ]; then
        echo "== Precompiling jdb_runtime.o =="
        $CXX -std=c++17 -O2 -DNDEBUG -Isrc -c src/jdb_runtime.cpp -o build/jdb_runtime.o
    fi

    # libjdbrt.so source list mirrors the main build minus main.cpp +
    # vm_bridge.cpp. Compile each to build/obj_pic/ with -fPIC, link as .so.
    mkdir -p build/obj_pic
    RT_SRC="src/vm_bridge.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
            src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
            src/gui.cpp src/ai.cpp src/llm.cpp src/channels.cpp src/file_streams.cpp \
            $HTTP_SRC $GFX_SRC $IMGUI_SRC $TUI_SRC"
    RT_FLAGS_HASH=$(echo "$CXX $CXXFLAGS -fPIC -DJDRT_EXPORTS" | sha1sum | cut -c1-12)
    RT_STAMP="build/obj_pic/.flags-$RT_FLAGS_HASH"
    if [ ! -f "$RT_STAMP" ]; then
        rm -f build/obj_pic/.flags-* 2>/dev/null
        rm -f build/obj_pic/*.o 2>/dev/null
        touch "$RT_STAMP"
    fi
    RT_OBJS=()
    RT_TO_BUILD=()
    for s in $RT_SRC; do
        o="build/obj_pic/$(basename "$s" .cpp).o"; RT_OBJS+=("$o")
        if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then RT_TO_BUILD+=("$s|$o"); fi
    done
    echo "== Building libjdbrt.so — ${#RT_TO_BUILD[@]} of ${#RT_OBJS[@]} stale =="
    if [ "${#RT_TO_BUILD[@]}" -gt 0 ]; then
        printf '%s\n' "${RT_TO_BUILD[@]}" | \
            xargs -P "$JOBS" -I{} bash -c '
                line="{}"; src="${line%%|*}"; obj="${line##*|}"
                echo "  CC (PIC) $src"
                '"$CXX"' '"$CXXFLAGS"' -fPIC -DJDRT_EXPORTS -c "$src" -o "$obj"
            '
    fi
    $CXX -shared -o build/libjdbrt.so "${RT_OBJS[@]}" $LDFLAGS
    echo "OK: build/libjdbrt.so"
fi
