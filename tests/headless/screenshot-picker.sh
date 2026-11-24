#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   foot, then Shift+Print            -> a frozen, dimmed copy of the screen
#   press at (300,200), drag to (700,450) and keep holding
# Expected screenshot: the picker mid-drag: the 400x250 area undimmed with
# an accent border and a "400 × 250" label below it, the hint pill on top.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/screenshot-picker.sh
set -eu

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys shift+Print
sleep 1.0
tetra-pointer hold 300 200 700 450 &
