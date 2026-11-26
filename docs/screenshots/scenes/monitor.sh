#!/usr/bin/env bash
# The system monitor's resources while something keeps one core busy.
# wait: 9
set -eu
timeout 20 bash -c 'while :; do :; done' &
tetra-monitor resources &
sleep 8
