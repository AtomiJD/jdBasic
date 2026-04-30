set -e
  mkdir -p build

  CXX=${CXX:-g++}
  CXXFLAGS="-std=c++17 -O2 -DNDEBUG -Isrc"
  LDFLAGS="-ldl -lpthread"

  SRC="src/main.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
       src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
       src/gui.cpp src/ai.cpp src/llm.cpp"

  echo "== Building jdBasic (console only, no GFX/IMGUI/HTTP/LLM/ONNX/NATIVEC) =="
  $CXX $CXXFLAGS $SRC -o build/jdbasic $LDFLAGS
  echo "OK: build/jdbasic"

