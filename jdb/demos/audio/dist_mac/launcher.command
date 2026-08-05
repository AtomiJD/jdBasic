#!/bin/bash
# Double-clicked from Finder: clear the download quarantine on this folder,
# then run the pedalboard from it.
cd "$(dirname "$0")" || exit 1
xattr -dr com.apple.quarantine . 2>/dev/null
./jdbasic fx_rack.jdb
