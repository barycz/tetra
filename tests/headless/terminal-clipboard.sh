#!/usr/bin/env bash
# Runs inside tetra at 1280x720. tetra-terminal and the clipboard:
#   echo pick-me-word; drag over it (row 1, x 304..388) -> the primary
#   selection; Ctrl+Shift+C -> the clipboard too
#   wl-copy "from-outside"; type "echo " and Ctrl+Shift+V, Return
#   report both selections (wl-paste) as a notification
# Expected screenshot: "pick-me-word" highlighted in the accent colour;
# "from-outside" echoed below; the notification shows
# "primary: pick-me-word clipboard: pick-me-word" (read before wl-copy).
#
#   scripts/run-headless.sh -w 7 -- tests/headless/terminal-clipboard.sh
# wait: 7
set -eu

tetra-terminal --title clipboard env PS1='$ ' HISTFILE=/dev/null bash --norc --noprofile &
sleep 2
tetra-sendkeys --type $'echo pick-me-word\n'
sleep 0.5
tetra-pointer drag 304 196 390 196
sleep 0.3
tetra-sendkeys ctrl+shift+c
sleep 0.3
selections="primary: $(wl-paste -p -n 2>&1) clipboard: $(wl-paste -n 2>&1)"
wl-copy "from-outside"
sleep 0.3
tetra-sendkeys --type 'echo '
tetra-sendkeys ctrl+shift+v Return
sleep 0.5
notify-send "Selections" "$selections"
