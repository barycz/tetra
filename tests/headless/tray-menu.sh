#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   tray item "orange", right click on it (1122,696) -> its dbusmenu
#   hover "More"                                   -> the submenu opens
# Expected screenshot: a menu above the icon: "Open window", separator,
# "Show details" with a check mark, "More" with its submenu open ("Item A",
# greyed "Disabled item"), "Quit".
#
#   scripts/run-headless.sh -w 6 -- tests/headless/tray-menu.sh
set -eu

sleep 1.0
tetra-tray-item orange --pixmap ff8800 &
sleep 1.0
tetra-pointer move 1122 696
wlrctl pointer click right
sleep 0.8
tetra-pointer move 1060 626
