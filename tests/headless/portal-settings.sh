#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The Settings portal, through
# xdg-desktop-portal and tetra-portal:
#   read color-scheme (dark: 1); watch it while config.json switches to a
#   light theme with an orange accent -> SettingChanged with 2
#   read the accent as RGB and as GNOME's accent name
# Expected screenshot: the desktop in the light theme with orange accents;
# a notification "before: 1 · changed: 2 · accent: (0.90…, 0.38…, 0.0)
# orange · font: Inter 10.5".
#
#   scripts/run-headless.sh -w 6 -- tests/headless/portal-settings.sh
# wait: 6
set -eu

before=$(portal-request read org.freedesktop.appearance color-scheme)
portal-request watch org.freedesktop.appearance color-scheme 5 >"$XDG_RUNTIME_DIR/watch.txt" &
watcher=$!
sleep 1.5
printf '{ "theme": { "dark": false, "accent": "#e66100" } }\n' >"$TETRA_CONFIG"
wait "$watcher" || true
accent=$(portal-request read org.freedesktop.appearance accent-color)
name=$(portal-request read org.gnome.desktop.interface accent-color)
font=$(portal-request read org.gnome.desktop.interface font-name)
notify-send "Portal settings" \
    "before: $before · $(tail -n 1 "$XDG_RUNTIME_DIR/watch.txt") · accent: $accent $name · font: $font"
