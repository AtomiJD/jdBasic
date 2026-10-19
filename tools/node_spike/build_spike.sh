#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
LIBS=""
[ "$(uname)" = "Linux" ] && LIBS="-lrt"
c++ -std=c++17 -O2 -pthread node_spike.cpp -o node_spike $LIBS
echo BUILD OK
