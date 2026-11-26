#!/usr/bin/env bash
# Shift+Print: a region screenshot, dragged on a frozen, dimmed screen.
# wait: 6
set -eu
tetra-settings appearance &
sleep 2.5
tetra-sendkeys shift+Print
sleep 1.2
tetra-pointer hold 260 140 900 520 &
sleep 1.0
