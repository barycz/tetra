#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with animations 20 times slower
# (TETRA_SLOW_ANIMATIONS=20): a notification, a second one 2.4 s later
# with the volume OSD, and a screenshot 0.4 s after that, while both move.
# Expected screenshot: at the bottom right, the first card in place and the
# second one fainter and shifted to the right (cut off by the screen edge);
# the volume OSD at the bottom centre, faint and a few pixels low.
#
#   TETRA_SLOW_ANIMATIONS=20 scripts/run-headless.sh -w 6 -- tests/headless/shell-animations.sh
# env: TETRA_SLOW_ANIMATIONS=20
# wait: 6
set -eu

sleep 1
notify-send -t 0 "First" "This one is in place by now"
sleep 2.4
notify-send -t 0 "Second" "Still sliding in from the right"
tetra-sendkeys XF86AudioRaiseVolume
sleep 0.4
grim "$XDG_RUNTIME_DIR/last.png"
