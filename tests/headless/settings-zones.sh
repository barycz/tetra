#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Settings > Snap zones:
#   a foot window opens and closes -> windows.json remembers it
#   tetra-settings zones
#   "Three columns" (773,270)      -> outputs: HEADLESS-1 gets "zones": "columns-3"
#   Gap slider (1000,638)          -> windows.zone_gap 25
#   wheel down; "Forget" (921,565) -> windows.json is deleted
#   report outputs, zone_gap and whether windows.json exists as a notification
# Expected screenshot: the page scrolled to Options, "Nothing remembered"
# greyed out; the notification shows
# { "name": "HEADLESS-1", "zones": "columns-3" }, "zone_gap": 25 and
# "windows.json: there, then gone".
#
#   scripts/run-headless.sh -w 9 -- tests/headless/settings-zones.sh
# wait: 9
set -eu

state="$XDG_STATE_HOME/tetra/windows.json"
foot --app-id forgetme &
f=$!
sleep 1
kill "$f"
sleep 1.5
before="$([ -f "$state" ] && echo there || echo missing)"

tetra-settings zones &
sleep 1.5
tetra-pointer click 773 270
sleep 0.4
tetra-pointer click 1000 638
sleep 0.4
tetra-pointer scroll 1050 480 10
sleep 0.4
tetra-pointer click 921 565
sleep 0.8
after="$([ -f "$state" ] && echo there || echo gone)"
notify-send "config.json" "$(grep -E 'HEADLESS|zone_gap' "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ') windows.json: $before, then $after"
