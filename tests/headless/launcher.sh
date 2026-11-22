#!/usr/bin/env bash
# Runs inside tetra at 1280x720: clicks the launcher button (x=357, y=696)
# and types "te" into the search field.
# Expected screenshot: launcher panel above the bar, search "te", the grid
# filtered to matching apps (e.g. terminals, text editors), first one selected.
# The pointer starts at (100,100); wlrctl moves are relative.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/launcher.sh
set -eu

sleep 1.0
wlrctl pointer move 257 596
wlrctl pointer click left
sleep 1.0
wlrctl keyboard type te
sleep 0.5
wlrctl pointer move 0 -350 # hover nothing
