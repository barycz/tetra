#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Mouse & touchpad writes "mouse" and
# "touchpad" (headless has no libinput devices to apply them to; the
# compositor log says "applied to 0 libinput device(s)").
#   tetra-settings mouse
#   Pointer speed (1010,160)      -> mouse.speed about 0.6
#   Acceleration (1050,203)       -> mouse.acceleration false
#   Primary button (921,356), Right (796,418) -> mouse.left_handed true
#   Tap to click (1050,530)       -> touchpad.tap_to_click false
#   report the "mouse" and "touchpad" members as a notification
# Expected screenshot: speed "Faster (+0.6)", Acceleration and Tap to click
# off, Primary button "Right"; the notification shows
# "speed": 0.6, "acceleration": false, "left_handed": true and
# "tap_to_click": false.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/settings-mouse.sh
# wait: 6
set -eu

tetra-settings mouse &
sleep 1.5
tetra-pointer click 1010 160
sleep 0.4
tetra-pointer click 1050 203
sleep 0.4
tetra-pointer click 921 356
sleep 0.4
tetra-pointer click 796 418
sleep 0.4
tetra-pointer click 1050 530
sleep 0.8
notify-send "config.json" "$(grep -A6 -E '"(mouse|touchpad)"' "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ')"
