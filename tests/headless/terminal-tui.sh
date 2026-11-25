#!/usr/bin/env bash
# Runs inside tetra at 1280x720. A full-screen program in tetra-terminal:
#   less on the numbers 1..1000 (alternate screen); 5 wheel notches down
#   -> 15 arrow-down keys -> less moves 15 lines
# Expected screenshot: less showing lines from 16 on, its status line at
# the bottom; no scroll indicator (full-screen programs get the wheel).
#
#   scripts/run-headless.sh -w 5 -- tests/headless/terminal-tui.sh
# wait: 5
set -eu

numbers="$XDG_RUNTIME_DIR/numbers.txt"
seq 1 1000 >"$numbers"
tetra-terminal --title tui env LESSHISTFILE=- less "$numbers" &
sleep 2
tetra-pointer scroll 640 400 5
sleep 0.6
