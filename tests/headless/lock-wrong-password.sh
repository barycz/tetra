#!/usr/bin/env bash
# Runs inside tetra at 1280x720; the shell checks passwords with pam_wrapper
# (see run-headless.sh), never against the real user database.
#   foot, then Super+L                -> lock screen covers everything
#   type a wrong password, Return     -> "Wrong password", still locked
#   Super+Return, Super+F1            -> ignored while locked
# Expected screenshot: lock screen (clock, date, user, empty field) with
# "Wrong password" below the field; no terminal, no bar.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/lock-wrong-password.sh
set -eu

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys super+l
sleep 0.8
wlrctl keyboard type "not-the-password"
tetra-sendkeys Return
sleep 0.8
tetra-sendkeys super+Return super+F1
