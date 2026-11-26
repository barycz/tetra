#!/usr/bin/env bash
# Runs inside tetra at 1280x720. A portal dialog belongs to the app's
# window (xdg-foreign v2): an "Editor" window snapped to the left half
# exports itself and asks for a screenshot with it as parent_window. The
# access dialog that xdg-desktop-portal shows first is centred over the
# Editor (not over the screen) and stays above it when the Editor is
# clicked (and focused).
# Expected screenshot: "Editor" filling the left half with an active title
# bar; the "Allow Applications to Take Screenshots?" dialog centred over
# the left half, on top of the Editor.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/portal-parent.sh
# wait: 6
set -eu

coproc PARENT { tetra-foreign-parent Editor; }
read -r handle <&"${PARENT[0]}"
sleep 0.5
tetra-sendkeys super+Left
sleep 0.5
portal-request screenshot --parent "$handle" >/dev/null &
sleep 2
tetra-pointer click 300 620 # the Editor, below the dialog
sleep 0.5
