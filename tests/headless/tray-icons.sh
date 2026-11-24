#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Apps' tray icons (StatusNotifierItem),
# from tetra-tray-item stand-ins:
#   orange pixmap item, Firefox-icon item, blue appindicator-style item
#   left click on the orange one (~1126,696)  -> "activate at ..."
#   middle click on it                        -> "secondary activate"
#   wheel up over it                          -> "scroll 120 vertical"
# Expected screenshot: three icons left of the network icon, right to left
# in start order; three notifications from "orange".
#
#   scripts/run-headless.sh -w 6 -- tests/headless/tray-icons.sh
set -eu

sleep 1.0
tetra-tray-item orange --pixmap ff8800 &
sleep 0.3
tetra-tray-item firefox --icon firefox-esr &
sleep 0.3
tetra-tray-item blue --pixmap 3070ff --appindicator &
sleep 1.0
tetra-pointer click 1126 696
sleep 0.5
wlrctl pointer click middle
sleep 0.5
wlrctl pointer scroll -10 0
