#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The system monitor's process list:
#   a CPU-busy program named burn-cpu-test, then Ctrl+Shift+Escape
#   -> tetra-monitor opens on Processes, sorted by CPU
#   type "burn" (no click first: typing searches) -> only burn-cpu-test
#   Down -> it is selected; Delete -> "End burn-cpu-test?"
# Expected screenshot: the monitor with "burn" in the search, one row
# (burn-cpu-test, about 100% / cores CPU), selected; the confirmation
# dialog in the middle ("End "burn-cpu-test"?", Cancel / End process).
#
#   scripts/run-headless.sh -w 6 -- tests/headless/monitor-processes.sh
# wait: 6
set -eu

burner="$XDG_RUNTIME_DIR/burn-cpu-test"
cp /usr/bin/yes "$burner"
timeout 15 "$burner" >/dev/null &  # ends on its own: nothing kills it with tetra
sleep 0.5
tetra-sendkeys ctrl+shift+Escape
sleep 2.5
tetra-sendkeys --type burn
sleep 0.3
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Delete
sleep 0.5
