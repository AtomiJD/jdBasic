#!/bin/sh
/mnt/d/usr/dev/cc/pico/build_pico.sh pico2_w calc > /tmp/one.log 2>&1 || { grep -iE "error" /tmp/one.log | grep -v cmake_install | head -10; exit 1; }
tail -1 /tmp/one.log
