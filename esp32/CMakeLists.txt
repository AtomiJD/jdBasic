cmake_minimum_required(VERSION 3.16)

# jdBasic on the ESP32-S3: the core interpreter behind a serial REPL,
# the same shape as pico/. Sources come from ../src; everything
# platform-shaped lives in main/.
#
# Two configurations, and the difference between them is the whole
# point of this port so far:
#
#   ./build.sh psram    512 KB SRAM + 8 MB PSRAM   (an S3R8 part)
#   ./build.sh nopsram  512 KB SRAM alone          (an S3FN8 part)
#
# The second is what a Cardputer would give us. Same binary otherwise.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(jdbasic_esp32)
