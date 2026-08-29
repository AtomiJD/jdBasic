#!/bin/sh
# Build the whole board matrix; one line per result.
cd "$(dirname "$0")"
for combo in "pico2_w calc" "pico2_w nocalc" "pico2 nocalc" "pico nocalc"; do
    if ./build_pico.sh $combo > /tmp/mtx.log 2>&1; then
        echo "OK   $combo  $(tail -1 /tmp/mtx.log | awk '{print $5, $NF}')"
    else
        echo "FAIL $combo"
        tail -25 /tmp/mtx.log
    fi
done
