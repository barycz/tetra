#!/usr/bin/env bash
# Runs inside tetra at 1280x720: a notification expiring after 1.5 s, and one
# with an "Open" action that is clicked (pointer starts at 100,100).
# Expected: the log contains "notify-send action: open"; the screenshot shows
# no notifications left (one expired, one closed by its action).
#
#   scripts/run-headless.sh -w 6 -- tests/headless/notification-actions.sh
set -eu

sleep 1.0
notify-send -t 1500 "Short-lived" "Gone in a moment."
answer=$(mktemp)
notify-send --action=open=Open "With an action" "Click Open." >"$answer" &
sender=$!
sleep 2.5 # the first one has expired; the action card sits alone above the bar

# Card: 380 px wide at x 888, bottom at y 660 (bar 48 + margin 12); with no
# icon, its Open button sits at the left of the text column, around (925,635).
wlrctl pointer move 825 535 # -> (925, 635)
sleep 0.3
wlrctl pointer click left
wait "$sender" || true
echo "notify-send action: $(cat "$answer")" >&2
rm -f "$answer"
wlrctl pointer move 0 -300
