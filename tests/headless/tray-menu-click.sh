#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   items "orange" (1122,696) and "blue" (1094,696, appindicator-style:
#   no Activate, so a left click opens its menu)
#   left click blue, click "Open window"   -> blue: "menu: _Open window"
#   right click orange, click "Quit"       -> orange: "menu: _Quit"; it exits
# Expected screenshot: only the blue icon left in the tray; the two
# notifications.
#
#   scripts/run-headless.sh -w 8 -- tests/headless/tray-menu-click.sh
# wait: 8
set -eu

sleep 1.0
tetra-tray-item orange --pixmap ff8800 &
sleep 0.3
tetra-tray-item blue --pixmap 3070ff --appindicator &
sleep 1.0
tetra-pointer click 1094 696
sleep 0.8
tetra-pointer click 1040 570
sleep 0.8
tetra-pointer move 1122 696
wlrctl pointer click right
sleep 0.8
tetra-pointer click 1060 649
sleep 1.0
tetra-pointer move 640 300
