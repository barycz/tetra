#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The monitor's Resources page while one
# core is busy:
#   burn-cpu-test running; tetra-monitor resources, sampled for ~4 s
# Expected screenshot: "Resources"; the CPU card with a graph that rises at
# the right edge, the summary "N% · M cores", one small graph per core
# (one of them near 100%); below, Memory and swap with its legend
# ("x of y (z%) · cache ..."); the sidebar's CPU and Memory mini graphs.
#
#   scripts/run-headless.sh -w 7 -- tests/headless/monitor-resources.sh
# wait: 7
set -eu

burner="$XDG_RUNTIME_DIR/burn-cpu-test"
cp /usr/bin/yes "$burner"
timeout 15 "$burner" >/dev/null &  # ends on its own: nothing kills it with tetra
tetra-monitor resources &
sleep 6
