set -e
  mkdir -p build

  CXX=${CXX:-g++}
  CXXFLAGS="-std=c++17 -O2 -DNDEBUG -Isrc"
  LDFLAGS="-ldl -lpthread"

  # Toggle modules from the command line: HTTP=1 ./build.sh, etc.
  WANT_HTTP=${HTTP:-1}

  if [ "$WANT_HTTP" = "1" ]; then
      CXXFLAGS="$CXXFLAGS -DHTTP -DCPPHTTPLIB_OPENSSL_SUPPORT"
      LDFLAGS="$LDFLAGS -lssl -lcrypto"
      HTTP_SRC="src/http.cpp"
  else
      HTTP_SRC=""
  fi

  SRC="src/main.cpp src/lexer.cpp src/parser.cpp src/compiler.cpp src/vm.cpp \
       src/console.cpp src/editor.cpp src/dap.cpp src/ffi.cpp src/sound.cpp \
       src/gui.cpp src/ai.cpp src/llm.cpp $HTTP_SRC"

  features="console"
  [ "$WANT_HTTP" = "1" ] && features="$features+HTTP"
  echo "== Building jdBasic ($features) =="
  $CXX $CXXFLAGS $SRC -o build/jdbasic $LDFLAGS
  echo "OK: build/jdbasic"

