#!/usr/bin/env bash
# Runs inside tetra with two 1280x720 outputs side by side (HEADLESS-2
# starts on the left, HEADLESS-1 on the right). Arranging by dragging:
#   tetra-settings displays
#   drag display 1 (HEADLESS-1, right box, 890,243) left of display 2 (470,243)
#   report the "outputs" lines as a notification
# Expected screenshot (2560x720): the outputs swapped places and Settings
# moved with its display (HEADLESS-2) to the right half, at x 1430; the
# arrangement shows 1 left of 2 under the banner asking to keep it; the
# notification shows HEADLESS-1 at [0, 0], HEADLESS-2 at [1280, 0].
#
#   scripts/run-headless.sh -n 2 -w 6 -- tests/headless/settings-displays-arrange.sh
# wait: 6
# outputs: 2
set -eu

tetra-settings displays &
sleep 1.5
tetra-pointer drag 890 243 470 243
sleep 1.0
notify-send "config.json" "$(grep -A3 outputs "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ')"
