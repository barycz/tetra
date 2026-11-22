#!/usr/bin/env bash
# Runs inside tetra at 1280x720 (bar layout: slot F1 centred at x=409,
# F2 at x=453, y=696). Exercises slot activation:
#   1. click F1 (foot stack, not focused) -> most recent foot ("second foot")
#   2. click F1 again                     -> cycles to the other foot ("foot")
#   3. click F2 (xterm, not focused)       -> xterm focused
#   4. click F2 again                      -> only window and focused: minimized
# Expected screenshot: "foot" focused, xterm hidden, F2 icon dimmed.
# The pointer starts at (100,100); wlrctl moves are relative.
#
#   scripts/run-headless.sh -d -w 6 -- tests/headless/taskbar-clicks.sh
set -eu

cat >"$TETRA_CONFIG" <<'EOF'
{ "taskbar": { "pinned": ["foot"] } }
EOF
sleep 0.5
foot &
sleep 0.3
foot -T "second foot" &
sleep 0.3
xterm -geometry 60x12 &
sleep 1.5

wlrctl pointer move 309 596 # F1
wlrctl pointer click left
sleep 0.4
wlrctl pointer click left
sleep 0.4
wlrctl pointer move 44 0    # F2
wlrctl pointer click left
sleep 0.4
wlrctl pointer click left
sleep 0.4
wlrctl pointer move 0 -300  # out of the bar, so no hover highlight
wait
