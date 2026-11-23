#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with the simulated sound device (50%).
#   click the tray speaker icon (~1182,696)    -> volume popup opens
#   click the mute button (~957,625)           -> muted, slider dimmed
#   Escape, click the speaker again            -> popup reopens, still muted
# Expected screenshot: popup above the tray ("Simulated output"), muted
# icon on the button and in the tray, slider at 50%.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/volume-popup.sh
set -eu

sleep 1.5
wlrctl pointer move 1082 596 # (100,100) -> (1182,696)
sleep 0.3
wlrctl pointer click left
sleep 0.8
wlrctl pointer move -225 -71 # -> (957,625): mute button
sleep 0.3
wlrctl pointer click left
sleep 0.5
tetra-sendkeys Escape
sleep 0.5
wlrctl pointer move 225 71 # back to the speaker
sleep 0.3
wlrctl pointer click left
