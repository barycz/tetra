#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   tetra-settings taskbar, open "Pin an app" (934,299), type "foot",
#   pick the first match
# Expected screenshot: "F1  Foot" pinned in the list with up/down/remove.
# The bar's F1 stays with the Settings window (open windows never move);
# foot takes F1 once it is free.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/settings-taskbar.sh
set -eu

sleep 1.0
tetra-settings taskbar &
sleep 1.5
tetra-pointer click 934 299
sleep 0.6
wlrctl keyboard type "foot"
sleep 0.6
tetra-pointer click 900 373
sleep 0.4
tetra-pointer move 640 600
