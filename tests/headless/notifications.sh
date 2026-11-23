#!/usr/bin/env bash
# Runs inside tetra (private session bus from run-headless.sh): sends four
# notifications with notify-send and checks the server over D-Bus.
# Expected screenshot: four cards stacked at the bottom-right above the bar:
#   "Welcome to tetra" (plain), "Download complete" (Firefox icon, two
#   buttons), "Battery low" (critical: red border), "Long text" (wrapped,
#   foot icon from a file path); newest at the bottom.
#
#   scripts/run-headless.sh -w 4 -- tests/headless/notifications.sh
set -eu

sleep 1.0
gdbus call --session --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.GetServerInformation >&2

notify-send "Welcome to tetra" "Notifications are served by tetra-shell."
# With actions, notify-send waits for an answer: keep it in the background.
notify-send -a "Firefox ESR" -i firefox-esr "Download complete" "report.pdf (2.3 MB)" \
    --action=open=Open --action=folder="Show in folder" >&2 &
sleep 0.2
notify-send -u critical -i battery-level-10-symbolic "Battery low" "10% remaining. Plug in soon."
id=$(notify-send -p -i /usr/share/icons/hicolor/48x48/apps/foot.png "Placeholder" "replaced next")
notify-send -r "$id" -i /usr/share/icons/hicolor/48x48/apps/foot.png "Long text" \
    "This body is long enough to wrap over several lines inside the card, so the popup has to measure its content and grow to fit it."
