#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with the "main-left" zone layout (a wide
# zone on the left, two stacked on the right). Dragging a window with Shift
# held shows the zones:
#   foot opens centred (title at y 89); hold Shift; press on the title
#   (500,89) and move to (1000,336), the edge between zones 2 and 3; keep
#   holding for the screenshot
# Expected screenshot: three numbered zones outlined in the accent colour
# over the desktop, 2 and 3 (right, top and bottom) filled: the window
# would span both; the window itself moved with the pointer.
#
#   scripts/run-headless.sh -w 4 -- tests/headless/zones-preview.sh
# wait: 4
set -eu

echo '{ "windows": { "zones": "main-left" } }' >"$TETRA_CONFIG"
sleep 0.3
tetra-pointer move 300 300
foot --app-id zonetest -T zones &
sleep 1.2
tetra-sendkeys --hold 10 shift &
sleep 0.4
tetra-pointer hold 500 89 1000 336 &
sleep 1.0
