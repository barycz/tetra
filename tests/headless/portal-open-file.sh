#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Opening a file through the portal:
#   Projects/, a.txt, notes.md: Down Down Return -> a.txt
#   then a second dialog: type "notes.md" (typing starts the location
#   field), Return -> notes.md; then a third: Escape -> cancelled
# Expected screenshot: no dialog; a notification
# "first: response 0 a.txt · second: response 0 notes.md · third: response 1".
#
#   scripts/run-headless.sh -w 9 -- tests/headless/portal-open-file.sh
# wait: 9
set -eu

dir="$XDG_RUNTIME_DIR/home/files"
mkdir -p "$dir/Projects"
printf 'hello\n' >"$dir/a.txt"
printf '# notes\n' >"$dir/notes.md"
picked() { # response line and the file name of the uri
    echo "$(head -n 1 "$1") $(sed -n "s|^uris: \['file://.*/\(.*\)'\]|\1|p" "$1")"
}

portal-request open --folder "$dir" >"$XDG_RUNTIME_DIR/first.txt" &
sleep 2.5
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Return
wait

portal-request open --folder "$dir" >"$XDG_RUNTIME_DIR/second.txt" &
sleep 1.5
tetra-sendkeys --type notes.md
sleep 0.3
tetra-sendkeys Return
wait

portal-request open --folder "$dir" >"$XDG_RUNTIME_DIR/third.txt" &
sleep 1.5
tetra-sendkeys Escape
wait || true

notify-send "Portal open" "first: $(picked "$XDG_RUNTIME_DIR/first.txt") · second: $(picked "$XDG_RUNTIME_DIR/second.txt") · third: $(head -n 1 "$XDG_RUNTIME_DIR/third.txt")"
