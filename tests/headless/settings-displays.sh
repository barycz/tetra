#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Displays: a change applies at once and
# waits to be kept.
#   tetra-settings displays
#   Scale (933,548) -> 125%       -> the desktop is drawn 25% bigger
#   report the "outputs" lines as a notification
# Expected screenshot: everything 25% bigger (Settings no longer fits),
# "Keep these display settings?" counting down; the notification shows
# { "name": "HEADLESS-1", "scale": 1.25 }.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/settings-displays.sh
# wait: 5
set -eu

tetra-settings displays &
sleep 1.5
tetra-pointer click 933 548
sleep 0.5
tetra-pointer click 900 406
sleep 1.0
notify-send "config.json" "$(grep -A2 outputs "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ')"
