#!/usr/bin/env bash
# Build SDL3 + SDL3_ttf + SDL3_image as static libs in libs/<lib>/build/.
# Run once after cloning the SDL3 repos. ImGui has no separate build —
# its .cpp files are compiled directly into jdbasic by build.sh.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
LIBS="$ROOT/libs"

JOBS=${JOBS:-$(nproc)}
COMMON_FLAGS="-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF"

build_one() {
    local name="$1"
    shift
    local extra="$@"
    echo "=== Building $name ==="
    if [ ! -d "$LIBS/$name" ]; then
        echo "MISSING: $LIBS/$name (clone it first)"; exit 1
    fi
    mkdir -p "$LIBS/$name/build"
    (cd "$LIBS/$name/build" && cmake $COMMON_FLAGS $extra ..)
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

echo
echo "All libs built. Static archives:"
find "$LIBS" -name "libSDL3*.a" 2>/dev/null
