#!/usr/bin/env bash
# Notifications (with actions and urgency) and the volume OSD.
# wait: 4
set -eu
sleep 1.0
notify-send -a "Firefox ESR" -i firefox-esr "Download complete" "report.pdf (2.3 MB)" \
    --action=open=Open --action=folder="Show in folder" >/dev/null &
sleep 0.2
notify-send -a Mail -i mail-unread-symbolic "3 new messages" "From the team: weekly notes, build status, lunch."
notify-send -a Power -u critical -i battery-level-10-symbolic "Battery low" "10% remaining. Plug in soon."
sleep 1.2
tetra-sendkeys XF86AudioRaiseVolume XF86AudioRaiseVolume
