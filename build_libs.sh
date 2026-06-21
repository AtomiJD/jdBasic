#!/usr/bin/env bash
# Build SDL3 + SDL3_ttf + SDL3_image as static libs in libs/<lib>/build/.
# Run once after cloning the SDL3 repos. ImGui has no separate build —
# its .cpp files are compiled directly into jdbasic by build.sh.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
LIBS="$ROOT/libs"

JOBS=${JOBS:-$(nproc)}
# Prefer Ninja, but fall back to Unix Makefiles when ninja isn't installed.
# Kept in its own variable (passed quoted) because "Unix Makefiles" contains a
# space that would otherwise word-split out of the unquoted $COMMON_FLAGS.
if command -v ninja >/dev/null 2>&1; then GEN="Ninja"; else GEN="Unix Makefiles"; fi
# PIC needed so the static archives can also be linked into libjdbrt.so
# (the runtime shared lib used by native-compiled executables).
COMMON_FLAGS="-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON"

build_one() {
    local name="$1"
    shift
    local extra="$@"
    echo "=== Building $name ==="
    if [ ! -d "$LIBS/$name" ]; then
        echo "MISSING: $LIBS/$name (clone it first)"; exit 1
    fi
    mkdir -p "$LIBS/$name/build"
    (cd "$LIBS/$name/build" && cmake -G "$GEN" $COMMON_FLAGS $extra ..)
    cmake --build "$LIBS/$name/build" -j "$JOBS"
    echo "done: $name"
}

# SDL3 first — others link against it. SDL_TEST disabled to skip tests.
build_one SDL3 \
    -DSDL_STATIC=ON -DSDL_SHARED=OFF \
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_INSTALL_TESTS=OFF

# Make SDL3_ttf and SDL3_image find our just-built SDL3 via SDL3_DIR
SDL3_CONFIG_DIR="$LIBS/SDL3/build"

build_one SDL3_ttf \
    -DSDL3_DIR="$SDL3_CONFIG_DIR" \
    -DSDLTTF_VENDORED=OFF \
    -DSDLTTF_HARFBUZZ=ON \
    -DSDLTTF_PLUTOSVG=OFF \
    -DSDLTTF_INSTALL=OFF \
    -DSDLTTF_SAMPLES=OFF

build_one SDL3_image \
    -DSDL3_DIR="$SDL3_CONFIG_DIR" \
    -DSDLIMAGE_VENDORED=OFF \
    -DSDLIMAGE_TIF=ON \
    -DSDLIMAGE_WEBP=ON \
    -DSDLIMAGE_AVIF=OFF \
    -DSDLIMAGE_INSTALL=OFF \
    -DSDLIMAGE_SAMPLES=OFF

# SDL3_mixer (graphics.cpp uses the SDL3-style 2-arg Mix_OpenAudio).
# main branch of github.com/libsdl-org/SDL_mixer targets SDL3.
build_one SDL3_mixer \
    -DSDL3_DIR="$SDL3_CONFIG_DIR" \
    -DSDLMIXER_VENDORED=OFF \
    -DSDLMIXER_INSTALL=OFF \
    -DSDLMIXER_SAMPLES=OFF \
    -DSDLMIXER_OPUS=OFF \
    -DSDLMIXER_GME=OFF \
    -DSDLMIXER_WAVPACK=OFF \
    -DSDLMIXER_MOD=OFF -DSDLMIXER_MOD_XMP=OFF \
    -DSDLMIXER_MIDI=OFF -DSDLMIXER_MIDI_FLUIDSYNTH=OFF \
    -DSDLMIXER_FLAC=OFF

# llama.cpp — built into libs/llama_src/build/, then headers + static
# archives copied/symlinked into libs/llama/ to match the directory shape
# expected by jdBasic's build flags. Skip when the source isn't there.
if [ -d "$LIBS/llama_src" ]; then
    echo "=== Building llama.cpp ==="
    mkdir -p "$LIBS/llama_src/build"
    (cd "$LIBS/llama_src/build" && cmake $COMMON_FLAGS \
        -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF -DLLAMA_CURL=OFF \
        -DGGML_OPENMP=OFF \
        ..)
    cmake --build "$LIBS/llama_src/build" -j "$JOBS"

    # Stage headers + .a files in libs/llama/ so the jdBasic build script
    # finds them at predictable paths (-Ilibs/llama, -Llibs/llama).
    mkdir -p "$LIBS/llama"
    cp -f "$LIBS/llama_src/include/llama.h" "$LIBS/llama/"
    cp -f "$LIBS/llama_src/ggml/include/"*.h "$LIBS/llama/" 2>/dev/null || true
    find "$LIBS/llama_src/build" -name "*.a" -exec cp -f {} "$LIBS/llama/" \;
    echo "done: llama"
fi

echo
echo "All libs built. Static archives:"
find "$LIBS" -maxdepth 4 -name "libSDL3*.a" -o -name "libllama*.a" -o -name "libggml*.a" 2>/dev/null
[ -f "$LIBS/onnxruntime/lib/libonnxruntime.so" ] && echo "$LIBS/onnxruntime/lib/libonnxruntime.so (prebuilt)"
