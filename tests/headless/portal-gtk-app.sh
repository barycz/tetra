#!/usr/bin/env bash
# Runs inside tetra at 1280x720. A GTK 4 app (zenity) asked to use portals
# opens tetra's file chooser, not GTK's own:
#   zenity --file-selection in a folder with a.txt: Down Return
# Expected screenshot: a notification "zenity picked: .../files/a.txt".
#
#   scripts/run-headless.sh -w 7 -- tests/headless/portal-gtk-app.sh
# wait: 7
set -eu

dir="$XDG_RUNTIME_DIR/home/files"
mkdir -p "$dir"
printf 'hello\n' >"$dir/a.txt"
GDK_DEBUG=portals zenity --file-selection --filename="$dir/" >"$XDG_RUNTIME_DIR/zenity.txt" 2>/dev/null &
sleep 3.5
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Return
wait || true
notify-send "zenity picked: $(cat "$XDG_RUNTIME_DIR/zenity.txt")"
