#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with animations 10 times slower
# (TETRA_SLOW_ANIMATIONS=10: opening 1.8 s, closing 1.5 s, minimising
# 2.2 s), so one screenshot catches all three halfway:
#   foot "left" snapped to the left half, foot "right" to the right half;
#   then "left" closes (Alt+F4), "right" is minimised (Super+Down) and a
#   third foot opens in the middle; the screenshot follows 0.5 s later.
# Expected screenshot: on the left, a fading copy of "left" shrinking
# towards its centre; on the right, a fading copy of "right" shrinking and
# dropping towards the bar; in the middle, the new foot half transparent
# (about 60 %) and a few pixels below its place, over both copies.
#
#   TETRA_SLOW_ANIMATIONS=10 scripts/run-headless.sh -w 9 -- tests/headless/window-animations.sh
# env: TETRA_SLOW_ANIMATIONS=10
# wait: 9
set -eu

# New windows open in the middle, not where the closed one was.
echo '{ "windows": { "remember_positions": false } }' >"$TETRA_CONFIG"
sleep 0.5
foot -T left env PS1='$ ' bash --norc --noprofile &
sleep 2.2
tetra-sendkeys super+Left
sleep 0.3
foot -T right env PS1='$ ' bash --norc --noprofile &
sleep 2.2
tetra-sendkeys super+Right
sleep 0.5
tetra-pointer click 300 400   # focus "left"
sleep 0.2
tetra-sendkeys alt+F4
sleep 0.1
tetra-pointer click 950 400   # focus "right"
tetra-sendkeys super+Down
foot -T middle env PS1='$ ' bash --norc --noprofile &
sleep 0.5
grim "$XDG_RUNTIME_DIR/last.png"
