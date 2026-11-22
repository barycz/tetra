#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Regression test for "bar stuck after the
# launcher was opened by clicking the logo":
#   click the logo (357,696)            -> launcher opens
#   click on the desktop (100,100)      -> launcher closes
#   click the logo again                -> launcher must open again
# Expected screenshot: launcher open; the logo is not stuck highlighted.
# The pointer starts at (100,100); wlrctl moves are relative.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/launcher-reopen.sh
set -eu

sleep 1.0
wlrctl pointer move 257 596
wlrctl pointer click left
sleep 0.8
wlrctl pointer move -257 -596
wlrctl pointer click left
sleep 0.8
wlrctl pointer move 257 596
wlrctl pointer click left
sleep 0.8
wlrctl pointer move 0 -400 # hover nothing in particular
