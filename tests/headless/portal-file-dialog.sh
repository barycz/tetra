#!/usr/bin/env bash
# Runs inside tetra at 1280x720. tetra's file chooser, opened through the
# FileChooser portal with two filters and several files allowed:
#   folder: Projects/, a.txt, b.png, notes.md, .hidden; filter "Text"
#   Down Down -> a.txt selected
# Expected screenshot: an "Open Files" window: places on the left (Home,
# Documents, Downloads, Pictures, Computer), the path "Home > files",
# Projects (folder icon), a.txt (selected), notes.md (no b.png, no
# .hidden), sizes and times; the "Text" filter, Cancel and Open.
#
#   scripts/run-headless.sh -w 5 -- tests/headless/portal-file-dialog.sh
# wait: 5
set -eu

home="$XDG_RUNTIME_DIR/home" # the portal's HOME in the tests
dir="$home/files"
mkdir -p "$home/Documents" "$home/Downloads" "$home/Pictures" "$dir/Projects"
printf 'hello\n' >"$dir/a.txt"
printf 'not really a picture' >"$dir/b.png"
seq 1 2000 >"$dir/notes.md"
touch "$dir/.hidden"
portal-request open --multiple --folder "$dir" --filter "Text:*.txt;*.md" --filter "Everything:*" &
sleep 2.5
tetra-sendkeys Down
sleep 0.2
tetra-sendkeys Down
sleep 0.5
