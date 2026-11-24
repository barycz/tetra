#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   config.json with a syntax error, then Settings
#   click "Light" (998,249, below the banner) -> refused; the file stays as it was
# Expected screenshot: Settings (dark) with a red-bordered banner "The change
# was not saved: config.json has an error; fix it first (...)"; the
# notification shows the file unchanged ("terminal": "foot",,).
#
#   scripts/run-headless.sh -w 6 -- tests/headless/settings-broken-config.sh
set -eu

printf '{\n  "terminal": "foot",,\n}\n' >"$TETRA_CONFIG"
sleep 1.0
tetra-settings &
sleep 1.5
tetra-pointer click 998 249
sleep 0.8
notify-send "config.json" "$(tr '\n' ' ' <"$TETRA_CONFIG")"
