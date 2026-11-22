#!/usr/bin/env bash
# Runs inside tetra at 1280x720: opens foot and xterm, clicks xterm's close
# button (X11 window destroyed through the caption button), then foot's
# minimize button, then opens a new foot.
# Expected screenshot: only the second foot window, focused, plus the bar.
# The pointer starts at (100,100); wlrctl moves are relative.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/window-ops.sh
set -eu

foot &
sleep 0.4
xterm -geometry 70x18 &
sleep 1.2

wlrctl pointer move 729 117   # -> (829,217): xterm close button
wlrctl pointer click left
sleep 0.5
wlrctl pointer move 44 -104   # -> (873,113): foot minimize button
wlrctl pointer click left
sleep 0.5
foot -T "second foot" &
wait
