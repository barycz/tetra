#!/usr/bin/env bash
# Runs inside `tetra --greeter` at 1280x720 with a fake greetd and logind
# (see run-headless.sh -g -l): made-up users, two sessions.
#   nothing typed yet; then "Shut down" is clicked -> logind PowerOff
# Expected screenshot: the login screen on the desktop colour: clock and
# date at the top; a card with an "A" avatar, "Alice Liddell" in the user
# list, an empty password field (focused), "Log in"; "Session tetra" at
# the bottom left; Suspend, Restart, Shut down at the bottom right.
#
#   scripts/run-headless.sh -g -l -w 4 -- tests/headless/greeter.sh
# greeter: yes
# logind: fake
# wait: 4
set -eu

sleep 2
tetra-pointer click 1216 683
sleep 0.5
grep -q '^PowerOff interactive=True$' "$XDG_RUNTIME_DIR/logind.log" ||
    echo "Shut down did not reach logind" >>"$XDG_RUNTIME_DIR/scenario-failed"
