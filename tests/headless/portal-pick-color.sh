#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The Screenshot portal's PickColor: the
# shell freezes the screen; a magnifier follows the pointer; a click on the
# desktop (background #2a6f97) picks its colour.
#   first: pointer to (300,300), screenshot of the picker (magnifier,
#   "#2a6f97", hint at the top) is taken while it waits...
#   then a click -> response 0 and color (0.16..., 0.43..., 0.59...)
# Expected screenshot: the frozen desktop with the magnifier near (300,300)
# showing #2a6f97, and the hint "Click to pick a colour · Esc to cancel";
# a notification from the first, already finished pick:
# "response 0 color: (0.1647…, 0.4352…, 0.5921…)".
#
#   scripts/run-headless.sh -w 7 -- tests/headless/portal-pick-color.sh
# wait: 7
set -eu

echo '{ "desktop": { "background": "#2a6f97" } }' >"$TETRA_CONFIG"
sleep 0.5
portal-request pick >"$XDG_RUNTIME_DIR/pick.txt" &
sleep 2
tetra-pointer click 300 300
wait
notify-send "Picked" "$(tr '\n' ' ' <"$XDG_RUNTIME_DIR/pick.txt")"
portal-request pick >/dev/null &
sleep 1.5
tetra-pointer move 320 280
sleep 0.5
