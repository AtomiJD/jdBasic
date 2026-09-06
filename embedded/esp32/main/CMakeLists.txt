# The interpreter comes from ../../../src unchanged; only the files in this
# directory know they are on an S3.
idf_component_register(
    SRCS
        "esp32_main.cpp"
        "esp32_builtins.cpp"
        "esp32_events.cpp"
        "esp32_fs.cpp"
        "esp32_wifi.cpp"
        "esp32_hw.cpp"
        # The 2.8 inch panel and its drawing verbs. Only the ES3C28P has one,
        # and a board without it never calls SCREEN, so the cost is code.
        "es3c28p_lcd.c"
        "es3c28p_con.c"
        "es3c28p_touch.c"
        "es3c28p_i2c.c"
        "es3c28p_snd.c"
        "es3c28p_sd.c"
        "es8311_vendor.c"
        "es3c28p_gfx.cpp"
        "esp32_stubs.cpp"
        # Everything that is the same job on every board: the prompt with
        # its command set and line editor, the full-screen editor, the
        # syntax colouring, the score engine behind PLAY, and the web
        # server over lwIP. Only the timer, the lock and the byte reader
        # under them are the platform's.
        "../../common/jdb_repl.cpp"
        "../../common/jdb_editor.cpp"
        "../../common/jdb_help.cpp"
        "../../common/jdb_syntax.cpp"
        "../../common/jdb_play.cpp"
        "../../common/jdb_httpd.cpp"
        "../../../src/lexer.cpp"
        "../../../src/parser.cpp"
        "../../../src/compiler.cpp"
        "../../../src/vm.cpp"
        "../../../src/jdb_embed_api.cpp"
        "../../../src/pcode.cpp"
        "../../../src/channels.cpp"
        "../../../src/file_streams.cpp"
    INCLUDE_DIRS "." "../../../src" "../../common"
    REQUIRES esp_timer driver fatfs vfs esp_vfs_console esp_driver_i2c esp_driver_i2s esp_driver_sdmmc sdmmc esp_wifi esp_netif nvs_flash esp_event lwip esp_lcd esp_http_client mbedtls
             esp_adc esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi
             esp_driver_tsens
)

# ESP32 is the platform switch, JDB_MCU the shared one: the same trades
# the RP2350 build takes, reserving token vectors and shrinking chunks.
# The RAM knobs, with their old values unless the build says otherwise;
# build.sh passes them as hist=8, histlen=128, fat.
if (NOT DEFINED JDB_HIST_N)
    set(JDB_HIST_N 16)
endif()
if (NOT DEFINED JDB_HIST_LEN)
    set(JDB_HIST_LEN 256)
endif()
if (NOT DEFINED JDB_LEAN)
    set(JDB_LEAN 1)
endif()
if (NOT DEFINED JDB_MAX_FRAMES)
    set(JDB_MAX_FRAMES 512)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32 JDB_MCU NDEBUG
    JDB_HIST_N=${JDB_HIST_N} JDB_HIST_LEN=${JDB_HIST_LEN} JDB_MAX_FRAMES=${JDB_MAX_FRAMES})
if (JDB_LEAN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE JDB_LEAN=1)
endif()

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
