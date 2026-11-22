#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Regression test for "clicking an app in the
# launcher does nothing" (a quick click must launch, without moving the
# pointer afterwards), and for closing an app via its title bar X:
#   Super tap, type "foot"                  -> launcher filtered to foot
#   move to the first grid cell (394,212),
#   click without moving afterwards         -> foot starts, launcher closes
#   click foot's close button (965,89)      -> foot closes, its slot clears
# Expected screenshot: empty desktop, all slots empty, launcher closed.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/launcher-click.sh
set -eu

sleep 1.0
tetra-sendkeys super
sleep 0.8
wlrctl keyboard type foot
sleep 0.5
wlrctl pointer move 294 112 # (100,100) -> (394,212)
sleep 0.3
wlrctl pointer click left
sleep 1.5
wlrctl pointer move 571 -123 # -> (965,89): close button of the new foot
sleep 0.3
wlrctl pointer click left
sleep 0.5
wlrctl pointer move 0 300
