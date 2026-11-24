#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Settings edits config.json in place:
#   a config with the user's own comments
#   Super+I                       -> Settings opens (Appearance)
#   click "Light" (998,163)       -> the whole desktop turns light
#   click the green accent (860,212)
#   report the file's comments and theme lines as a notification
# Expected screenshot: light Settings, bar and decorations with a green
# accent; the notification shows both comments kept and the new theme.
#
#   scripts/run-headless.sh -w 7 -- tests/headless/settings-edit.sh
# wait: 7
set -eu

cat >"$TETRA_CONFIG" <<'JSON'
// my own comment
{
  "terminal": "foot" // keep me
}
JSON
sleep 1.0
tetra-sendkeys super+i
sleep 1.5
tetra-pointer click 998 163
sleep 0.8
tetra-pointer click 860 212
sleep 0.8
notify-send "config.json" "$(grep -E '//|dark|accent' "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ')"
