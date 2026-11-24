#!/usr/bin/env bash
# Runs inside tetra at 1280x720. A display change can be taken back:
#   a commented config; tetra-settings displays
#   Scale (933,548) -> 125% (900,406)
#   Revert (1140,157; the screen is scaled by then)
#   report config.json as a notification
# Expected screenshot: scale 100% again, no "Keep these display
# settings?" banner; the notification shows the file exactly as it was
# (comment included, no "outputs").
#
#   scripts/run-headless.sh -w 6 -- tests/headless/settings-displays-revert.sh
# wait: 6
set -eu

cat >"$TETRA_CONFIG" <<'JSON'
{
  "terminal": "foot" // mine
}
JSON
sleep 0.5
tetra-settings displays &
sleep 1.5
tetra-pointer click 933 548
sleep 0.5
tetra-pointer click 900 406
sleep 1.0
tetra-pointer click 1140 157
sleep 1.0
notify-send "config.json" "$(tr -s ' ' <"$TETRA_CONFIG" | tr '\n' ' ')"
