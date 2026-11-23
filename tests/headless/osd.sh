#!/usr/bin/env bash
# Runs inside tetra at 1280x720 with the simulated sound device (50%).
# Volume keys go compositor -> shell (tetra-shell media_key) -> OSD:
#   XF86AudioRaiseVolume x2     -> 60%, OSD at the bottom centre
# Expected screenshot (taken ~6 s after start, while the OSD is still up):
# OSD with the medium speaker icon and the bar at 60%; tray icon medium.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/osd.sh
set -eu

sleep 4.8
tetra-sendkeys XF86AudioRaiseVolume XF86AudioRaiseVolume
