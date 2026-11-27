#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with a fake logind (power actions only,
# see run-headless.sh -l).
#   Super (the launcher), click "Power"   -> a menu above the button: Lock,
#                                            Log out, Suspend, Restart,
#                                            Shut down (screenshot)
#   click "Restart"                       -> logind Reboot(interactive)
# Expected screenshot: the launcher with the power menu open over the
# bottom right of the app grid, an icon before each entry; "Power"
# highlighted in the footer.
#
#   scripts/run-headless.sh -l -w 5 -- tests/headless/launcher-power.sh
# logind: fake
# wait: 5
set -eu

sleep 1
tetra-sendkeys super
sleep 0.6
tetra-pointer click 895 636
sleep 0.5
grim "$XDG_RUNTIME_DIR/last.png"
tetra-pointer click 840 564
sleep 0.5
grep -q '^Reboot interactive=True$' "$XDG_RUNTIME_DIR/logind.log" ||
    echo "Restart did not reach logind" >>"$XDG_RUNTIME_DIR/scenario-failed"
