#!/usr/bin/env bash
# Stage and tar a POSIX distribution bundle from an already built tree.
#
#   tools/pack_posix.sh jdbasic-linux-x64
#   tools/pack_posix.sh jdbasic-macos-arm64
#
# Expects build/jdbasic, build/libjdbrt.so and build/jdb_runtime.o to exist
# (build.sh with NATIVEC=1 produces all three). Writes
# dist/<name>.tar.gz plus its .sha256 and prints the hash.
set -eu

NAME="${1:-}"
if [ -z "$NAME" ]; then
    echo "usage: $0 <bundle-name>" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

STAGE="dist/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/doc"

for f in build/jdbasic build/libjdbrt.so build/jdb_runtime.o; do
    if [ ! -f "$f" ]; then
        echo "missing $f - build with NATIVEC=1 first" >&2
        exit 1
    fi
done

cp build/jdbasic build/libjdbrt.so build/jdb_runtime.o "$STAGE/"
cp help.txt LICENSE.txt THIRD_PARTY_LICENSES.txt jdbasic_default.ttf "$STAGE/"
cp doc/languages.md doc/MCP.md "$STAGE/doc/"
cp -r lib "$STAGE/lib"

VER="$("./build/jdbasic" --version 2>&1 | head -1)"
FEAT="$("./build/jdbasic" --version 2>&1 | sed -n 's/^Features: *//p')"
cat > "$STAGE/README.txt" <<TXT
$VER
Features: $FEAT
Built on: $(uname -srm)

Unpack anywhere and run from this directory:

  ./jdbasic                 the REPL
  ./jdbasic hello.jdb       run a program
  ./jdbasic -c hello.jdb    compile it, then ./hello.exe
  ./jdbasic --mcp           serve as an MCP server
  ./jdbasic --dump-help     the full command reference (also in help.txt)

libjdbrt.so sits beside the binary and is found from there, so no library
path has to be set and a compiled program runs from any directory. The
module library in lib/ is found the same way: IMPORT DF works out of the
box, and JDBASIC_PATH=<dir> points a program at another copy.

OpenSSL is not bundled: the binary links the system libssl and libcrypto.

Language reference: doc/languages.md, MCP setup: doc/MCP.md
Source and issues: https://github.com/AtomiJD/jdBasic
TXT

tar -czf "dist/$NAME.tar.gz" -C dist "$NAME"

if command -v sha256sum >/dev/null 2>&1; then
    (cd dist && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
else
    (cd dist && shasum -a 256 "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
fi

ls -l "dist/$NAME.tar.gz"
cat "dist/$NAME.tar.gz.sha256"
