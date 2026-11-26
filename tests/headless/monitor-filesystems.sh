#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The monitor's File systems page.
# Expected screenshot: "File systems"; a table with at least "/" (its
# device, type, size, available) and a usage bar with "used  N%".
#
#   scripts/run-headless.sh -w 4 -- tests/headless/monitor-filesystems.sh
# wait: 4
set -eu

tetra-monitor filesystems &
sleep 3
