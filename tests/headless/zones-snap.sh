#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with the "columns-3" zone layout (gap 8:
# zones at x 8, 432 and 856, each 416 wide, y 8 to 664 above the bar).
#   foot "A": Shift+drag its title (500,89) to (1100,300) -> fills zone 3
#   Super+Left -> A moves to zone 2
#   foot "B": Shift+drag its title (500,89) to (150,300) -> fills zone 1
#   B dragged again without Shift, from (200,25) to (1000,200) -> out of its
#   zone, back to its own size (696x494)
# Expected screenshot: A filling the middle third (frame 432..848 x 8..664);
# B at its own size with its title under the pointer near (1000,200),
# partly over A; the left third empty.
#
#   scripts/run-headless.sh -w 8 -- tests/headless/zones-snap.sh
# wait: 8
set -eu

shift_drag() { # x1 y1 x2 y2
    tetra-sendkeys --hold 1.2 shift &
    sleep 0.3
    tetra-pointer drag "$@"
    wait $!
}

echo '{ "windows": { "zones": "columns-3" } }' >"$TETRA_CONFIG"
sleep 0.3
tetra-pointer move 300 300
foot --app-id zonetest -T A &
sleep 1.2
shift_drag 500 89 1100 300
sleep 0.4
tetra-sendkeys super+Left
sleep 0.4

foot --app-id zonetest -T B &
sleep 1.2
shift_drag 500 89 150 300
sleep 0.4
tetra-pointer drag 200 25 1000 200
sleep 0.6
