#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   foot; Super (the launcher), "Power", "Log out" (screenshot first)
#                       -> the shell sends tetra_shell_v1.quit and the
#                          compositor exits with status 0 (run-headless.sh
#                          checks that and keeps the screenshot)
# Expected screenshot: foot behind the launcher, the power menu open with
# "Log out" under the pointer.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/session-logout.sh
# wait: 5
set -eu

foot env PS1='$ ' bash --norc --noprofile &
sleep 1
tetra-sendkeys super
sleep 0.6
tetra-pointer click 895 636
sleep 0.4
tetra-pointer move 840 504
sleep 0.3
grim "$XDG_RUNTIME_DIR/last.png"
tetra-pointer click 840 504
