#!/usr/bin/env bash
# wait: 11
# Runs inside tetra at 1280x720. The lock screen crashes while locked:
#   foot, Super+L                          -> locked
#   kill -9 this compositor's tetra-shell  -> the session stays covered
#   (the compositor restarts the shell, which locks again on its own)
#   type the test password, Return         -> unlocked
# Expected screenshot: the desktop with foot; the bar is back.
#
#   scripts/run-headless.sh -w 11 -- tests/headless/lock-recovery.sh
set -eu

# Our compositor: the nearest "tetra" ancestor (sh -c may sit in between).
compositor=$$
while [[ "$(ps -o comm= -p "$compositor")" != "tetra" ]]; do
    compositor="$(ps -o ppid= -p "$compositor" | tr -d ' ')"
    [[ "$compositor" -gt 1 ]] || { echo "no tetra ancestor" >&2; exit 1; }
done

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys super+l
sleep 1.0
pkill -KILL -P "$compositor" -x tetra-shell
sleep 5.0 # shell restart back-off (1 s) plus startup, under ASan too
wlrctl keyboard type "$TETRA_TEST_PASSWORD"
tetra-sendkeys Return
