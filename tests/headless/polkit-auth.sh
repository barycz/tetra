#!/usr/bin/env bash
# Runs inside tetra at 1280x720 (see polkit-wrong-password.sh).
#   a request offering root and the current user -> the user is preselected
#   type the test password, Return               -> the request succeeds
#   a second request, Escape                     -> it is cancelled
# Each outcome is shown as a notification.
# Expected screenshot: no dialog; notifications "polkit: ()" and
# "polkit: ...Error.Cancelled...".
#
#   scripts/run-headless.sh -w 6 -- tests/headless/polkit-auth.sh
set -eu

report() { notify-send "polkit" "$(polkit-request "$@" 2>&1 | tr -d '\n' | cut -c1-90)"; }

sleep 1.0
report "[('unix-user', {'uid': <uint32 0>}), ('unix-user', {'uid': <uint32 $(id -u)>})]" &
sleep 1.2
wlrctl keyboard type "$TETRA_TEST_PASSWORD"
tetra-sendkeys Return
sleep 1.0
report &
sleep 1.0
tetra-sendkeys Escape
