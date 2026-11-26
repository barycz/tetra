#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Ending a process from the monitor:
#   burn-cpu-test running; tetra-monitor --search burn-cpu
#   Down, Delete, Return -> SIGTERM; the row goes away
#   a notification says whether the process is gone
# Expected screenshot: the monitor with "burn-cpu" in the search and no
# rows ("0 of N processes"); a notification "burn-cpu-test: ended".
#
#   scripts/run-headless.sh -w 8 -- tests/headless/monitor-end.sh
# wait: 8
set -eu

burner="$XDG_RUNTIME_DIR/burn-cpu-test"
cp /usr/bin/yes "$burner"
timeout 15 "$burner" >/dev/null &  # ends on its own: nothing kills it with tetra

tetra-monitor --search burn-cpu &
sleep 3
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Delete
sleep 0.3
tetra-sendkeys Return
sleep 1
if pgrep -x burn-cpu-test >/dev/null; then
    notify-send "burn-cpu-test: still running"
else
    notify-send "burn-cpu-test: ended"
fi
