#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The Screenshot portal, taken by the shell:
#   a terminal for something to see
#   interactive: the region picker; drag (300,200)-(700,450) -> response 0
#   and a file:// URI under Pictures/Screenshots
#   then a non-interactive one: xdg-desktop-portal first asks through
#   tetra-portal's access dialog ("Allow"); Enter -> response 0
# Expected screenshot: the terminal; two "Screenshot taken · for an app"
# notifications from the shell and one with both results:
# "interactive: response 0 400x250 · full: response 0 1280x720".
#
#   scripts/run-headless.sh -w 10 -- tests/headless/portal-screenshot.sh
# wait: 10
set -eu

tetra-terminal --title shots env PS1='$ ' bash --norc --noprofile -c 'seq 1 40; exec bash --norc' &
sleep 2
portal-request screenshot --interactive >"$XDG_RUNTIME_DIR/interactive.txt" &
picker=$!
sleep 1.5
tetra-pointer drag 300 200 700 450
wait "$picker" || true
portal-request screenshot >"$XDG_RUNTIME_DIR/full.txt" &
full=$!
sleep 2
tetra-sendkeys Return # the access dialog, if xdg-desktop-portal asks
wait "$full" || true

size() { # the PNG behind the uri: line of a result
    local file
    file=$(sed -n 's|^uri: file://||p' "$1" | python3 -c 'import sys, urllib.parse; print(urllib.parse.unquote(sys.stdin.read().strip()))')
    [[ -f "$file" ]] && python3 -c 'import struct, sys; d = open(sys.argv[1], "rb").read(24); print("%dx%d" % struct.unpack(">II", d[16:24]))' "$file"
}
notify-send "Portal screenshots" \
    "interactive: $(head -n 1 "$XDG_RUNTIME_DIR/interactive.txt") $(size "$XDG_RUNTIME_DIR/interactive.txt") · full: $(head -n 1 "$XDG_RUNTIME_DIR/full.txt") $(size "$XDG_RUNTIME_DIR/full.txt")"
