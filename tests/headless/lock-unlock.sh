#!/usr/bin/env bash
# Runs inside tetra at 1280x720; the shell checks passwords with pam_wrapper
# (see run-headless.sh): only $TETRA_TEST_PASSWORD is accepted.
#   foot, then Super+L                 -> locked
#   type the test password, Return     -> unlocked
#   type "ok"                          -> keyboard focus is back on foot
# Expected screenshot: the desktop with foot focused (its slot highlighted),
# "ok" typed at its prompt; no lock screen.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/lock-unlock.sh
set -eu

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys super+l
sleep 0.8
wlrctl keyboard type "$TETRA_TEST_PASSWORD"
tetra-sendkeys Return
sleep 1.0
wlrctl keyboard type "ok"
