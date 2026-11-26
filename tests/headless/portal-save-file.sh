#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Saving through the portal:
#   suggested name report.txt (its "report" is selected): type "draft",
#   Return -> draft.txt
#   then report.txt again, which exists: Return -> "Replace?" asks
# Expected screenshot: a "Save File" window with Name "report.txt" and a
# "Replace "report.txt"?" box (Cancel / Replace); a notification
# "first: response 0 draft.txt".
#
#   scripts/run-headless.sh -w 7 -- tests/headless/portal-save-file.sh
# wait: 7
set -eu

dir="$XDG_RUNTIME_DIR/home/files"
mkdir -p "$dir"
printf 'old\n' >"$dir/report.txt"

portal-request save --folder "$dir" --name report.txt >"$XDG_RUNTIME_DIR/first.txt" &
sleep 2.5
tetra-sendkeys --type draft
sleep 0.2
tetra-sendkeys Return
wait
notify-send "Portal save" "first: $(head -n 1 "$XDG_RUNTIME_DIR/first.txt") $(sed -n "s|^uris: \['file://.*/\(.*\)'\]|\1|p" "$XDG_RUNTIME_DIR/first.txt")"

portal-request save --folder "$dir" --name report.txt &
sleep 1.5
tetra-sendkeys Return
sleep 0.5
