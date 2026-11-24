#!/usr/bin/env bash
# Runs inside tetra at 1280x720. wlr-output-management is read-only:
#   wlr-randr                          -> lists HEADLESS-1 (1280x720, scale 1)
#   wlr-randr --output HEADLESS-1 --scale 2   -> refused (config.json decides)
#   report both as a notification
# Expected screenshot: scale 1; the notification shows "HEADLESS-1",
# "Scale: 1.000000" and "refused".
#
#   scripts/run-headless.sh -w 4 -- tests/headless/output-management.sh
# wait: 4
set -eu

sleep 1.0 # the shell (notifications) starts first

listing="$(wlr-randr 2>&1 | grep -E '^HEADLESS|Scale|Position' | tr -s ' ' | tr '\n' ' ')"
if wlr-randr --output HEADLESS-1 --scale 2 >/dev/null 2>&1; then
    result="applied (wrong)"
else
    result="refused"
fi
notify-send "wlr-randr" "$listing; change: $result"
