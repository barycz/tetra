#!/usr/bin/env bash
# Snap zones: dragging a window with Shift held shows the layout's zones
# (here "priority"); the zone under the pointer is highlighted.
# wait: 4
set -eu
echo '{ "windows": { "zones": "priority" } }' >"$TETRA_CONFIG"
sleep 0.3
tetra-pointer move 300 300
foot -T zones env PS1='$ ' bash --norc --noprofile & # a neutral prompt: no user or host names
sleep 1.2
tetra-sendkeys --hold 10 shift &
sleep 0.4
tetra-pointer hold 500 89 640 300 &
sleep 1.0
