#!/usr/bin/env bash
# Two short tetra runs on the real display to find out why the cursor is
# drawn in software (run from a free TTY, not over SSH).
#   1. atomic KMS (default), verbose log  -> ~/cursor-atomic.log
#   2. legacy KMS (WLR_DRM_NO_ATOMIC=1)   -> ~/cursor-legacy.log
# Each run starts by itself: move the mouse around for ~10 seconds, then
# quit with Super+Shift+Escape. Note which run's pointer feels better.
set -u
bin="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/release/src/compositor/tetra"

countdown() {
    echo "$1"
    for i in 3 2 1; do
        echo "  starting in $i..."
        sleep 1
    done
}

countdown "Run 1 of 2 (atomic): move the mouse ~10 s, then Super+Shift+Escape."
TETRA_STATS=1 "$bin" -d 2>"$HOME/cursor-atomic.log"
echo "Run 1 exited with status $?."

countdown "Run 2 of 2 (legacy): same again."
WLR_DRM_NO_ATOMIC=1 TETRA_STATS=1 "$bin" -d 2>"$HOME/cursor-legacy.log"
echo "Run 2 exited with status $?."

echo "Done. Logs: ~/cursor-atomic.log ~/cursor-legacy.log"
