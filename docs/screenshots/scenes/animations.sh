#!/usr/bin/env bash
# Window animations caught halfway, 10 times slower: a window closing
# (left), one opening (middle), one minimising towards the bar (right).
# env: TETRA_SLOW_ANIMATIONS=10
# wait: 9
set -eu
echo '{ "windows": { "remember_positions": false } }' >"$TETRA_CONFIG"
sleep 0.5
tetra-terminal --title Notes env PS1='$ ' bash --norc --noprofile &
sleep 2.2
tetra-sendkeys super+Left
sleep 0.3
tetra-monitor resources &
sleep 2.2
tetra-sendkeys super+Right
sleep 0.5
tetra-pointer click 300 400
sleep 0.2
tetra-sendkeys alt+F4
sleep 0.1
tetra-pointer click 950 400
tetra-sendkeys super+Down
tetra-terminal --title Opening env PS1='$ ' bash --norc --noprofile &
sleep 0.5
grim "$XDG_RUNTIME_DIR/last.png"
