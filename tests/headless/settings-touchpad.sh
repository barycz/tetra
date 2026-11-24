#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Mouse & touchpad, scrolled to the touchpad:
#   tetra-settings mouse; wheel down over the page
#   Scrolling (921,500), "Along the right edge" (850,562) -> touchpad.scroll "edge"
#   Right click (921,544), "Two fingers" (850,626)        -> touchpad.click "fingers"
#   Right click (921,544), "Touchpad default" (850,586)   -> touchpad.click removed
#   Disable while typing (1050,606)                       -> touchpad.disable_while_typing false
#   report "touchpad" after the second step and at the end as a notification
# Expected screenshot: Scrolling "Along the right edge", Right click
# "Touchpad default", Disable while typing off; the notification shows
# then: "scroll": "edge", "click": "fingers" and now: "scroll": "edge",
# "disable_while_typing": false (no "click").
#
#   scripts/run-headless.sh -w 7 -- tests/headless/settings-touchpad.sh
# wait: 7
set -eu

touchpad() { grep -A6 '"touchpad"' "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' '; }

tetra-settings mouse &
sleep 1.5
tetra-pointer scroll 745 400 10
sleep 0.3
tetra-pointer click 921 500
sleep 0.4
tetra-pointer click 850 562
sleep 0.4
tetra-pointer click 921 544
sleep 0.4
tetra-pointer click 850 626
sleep 0.6
then="$(touchpad)"
tetra-pointer click 921 544
sleep 0.4
tetra-pointer click 850 586
sleep 0.4
tetra-pointer click 1050 606
sleep 0.8
notify-send "config.json" "then: $then now: $(touchpad)"
