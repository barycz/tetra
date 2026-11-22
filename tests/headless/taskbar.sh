#!/usr/bin/env bash
# Runs inside tetra at 1280x720: pins foot and Firefox, opens two foot
# windows (they stack in pinned slot F1) and an xterm (first free slot, F3).
# Expected screenshot: F1 foot with a "2" badge, F2 Firefox pinned (no
# indicator), F3 xterm focused (accent indicator), F4-F12 empty outlines.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/taskbar.sh
set -eu

cat >"$TETRA_CONFIG" <<'EOF'
{ "taskbar": { "pinned": ["foot", "firefox-esr"] } }
EOF
sleep 0.5
foot &
sleep 0.3
foot -T "second foot" &
sleep 0.3
xterm -geometry 60x12 &
wait
