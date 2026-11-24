#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The shell's polkit agent, asked the way
# polkitd would (polkit-request); the helper is a test stand-in that
# accepts only $TETRA_TEST_PASSWORD.
#   foot, then a request for org.freedesktop.policykit.exec
#   -> dimmed screen, dialog: icon, message, "Authenticating as <user>"
#   type a wrong password, Return -> "Wrong password. Try again."
# Expected screenshot: the dialog with that error and an empty field.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/polkit-wrong-password.sh
set -eu

sleep 1.0
foot &
sleep 1.0
polkit-request >/dev/null 2>&1 &
sleep 1.2
wlrctl keyboard type "not-the-password"
tetra-sendkeys Return
