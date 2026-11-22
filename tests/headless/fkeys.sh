#!/usr/bin/env bash
# Runs inside tetra: drives the task bar from the keyboard.
#   foot pinned to F1, xterm lands in F2 (focused)
#   Super+F1        -> foot focused
#   Super+F1        -> foot is the slot's only, focused window: minimized
#   Super+Shift+F2  -> a second xterm (F2 badge "2")
#   Super (tap)     -> launcher opens
# Expected screenshot: launcher open; bar shows F1 foot dimmed (minimized),
# F2 xterm with a "2" badge.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/fkeys.sh
set -eu

cat >"$TETRA_CONFIG" <<'EOF'
{ "taskbar": { "pinned": ["foot"] } }
EOF
sleep 0.5
foot &
sleep 0.3
xterm -geometry 60x12 &
sleep 1.5

tetra-sendkeys super+F1 super+F1 super+shift+F2
sleep 1.5
tetra-sendkeys super
wait
