#!/usr/bin/env bash
# The desktop: the terminal and the system monitor snapped side by side
# (Super+Left / Super+Right), apps pinned to F-key slots.
# wait: 11
set -eu
cat >"$TETRA_CONFIG" <<'CONFIG'
{ "taskbar": { "pinned": ["tetra-terminal", "tetra-monitor", "tetra-settings"] } }
CONFIG
sleep 0.5
tetra-terminal --title "~/src/tetra" env PS1='\[\e[1;34m\]~/src/tetra\[\e[0m\] $ ' HISTFILE=/dev/null \
    bash --norc --noprofile &
sleep 2
tetra-sendkeys --type $'git log --oneline -12\n'
sleep 0.4
tetra-sendkeys --type $'ls --color=always src\n'
sleep 0.3
tetra-sendkeys super+Left
timeout 30 bash -c 'while :; do :; done' &
tetra-monitor resources &
sleep 2
tetra-sendkeys super+Right
sleep 4
