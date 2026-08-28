# The interpreter comes from ../../src unchanged; only the files in this
# directory know they are on an S3.
idf_component_register(
    SRCS
        "esp32_main.cpp"
        "esp32_builtins.cpp"
        "esp32_events.cpp"
        "esp32_fs.cpp"
        "esp32_stubs.cpp"
        # Colouring one line of jdBasic is the same job on both boards and
        # depends on nothing either SDK provides. Shared rather than copied;
        # if a third caller turns up it moves somewhere neutral.
        "../../pico/pico_syntax.cpp"
        "../../src/lexer.cpp"
        "../../src/parser.cpp"
        "../../src/compiler.cpp"
        "../../src/vm.cpp"
        "../../src/jdb_embed_api.cpp"
        "../../src/channels.cpp"
        "../../src/file_streams.cpp"
    INCLUDE_DIRS "." "../../src"
    REQUIRES esp_timer driver fatfs vfs
)

# ESP32 is the platform switch, JDB_MCU the shared one: the same trades
# the RP2350 build takes, reserving token vectors and shrinking chunks.
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32 JDB_MCU NDEBUG)

# IDF compiles with -Werror=all, and GCC's -Wno-error=all does not undo
# that: the specific forms have to be named. These are style warnings on
# code that is correct and builds clean everywhere else, plus xtensa's
# uint32_t being unsigned long, which makes the interpreter's %x formats
# warn. They stay visible as warnings. IDF's own components keep -Werror.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os
    -Wno-error=parentheses -Wno-error=misleading-indentation
    -Wno-error=format -Wno-error=shadow -Wno-error=logical-op
    -Wno-error=missing-declarations -Wno-error=vla
    -Wno-error=write-strings -Wno-error=char-subscripts
    -Wno-error=maybe-uninitialized -Wno-format
    -include "${CMAKE_CURRENT_LIST_DIR}/jdb_esp32_compat.h")
