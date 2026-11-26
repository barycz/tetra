#!/usr/bin/env bash
# Runs inside tetra at 1280x720, with a private PipeWire. Screen sharing
# through the ScreenCast portal: an app (portal-request) asks to share a
# display with the pointer; tetra-portal asks "Share your screen?" (Enter
# shares); one frame of the PipeWire stream is saved as a PNG; the session
# stays open, so the shell shows that the screen is being shared.
# Expected screenshot: a terminal; a "Your screen is being shared"
# notification (Screen sharing, HEADLESS-1); a notification with
# "response 0 · streams: node N ... · frame: 1280x720".
#
#   scripts/run-headless.sh -a -w 18 -- tests/headless/portal-screencast.sh
# wait: 18
# audio: pipewire
set -eu

tetra-terminal --title shared env PS1='$ ' bash --norc --noprofile -c 'seq 1 30; exec bash --norc' &
sleep 2
portal-request screencast --cursor --hold 14 --out "$XDG_RUNTIME_DIR/frame.png" >"$XDG_RUNTIME_DIR/cast.txt" 2>&1 &
sleep 2.5
tetra-sendkeys Return # the share dialog
for _ in $(seq 60); do
    grep -q '^frame\|^no frame' "$XDG_RUNTIME_DIR/cast.txt" && break
    sleep 0.2
done
notify-send -t 0 "Screencast" "$(tr '\n' ' ' <"$XDG_RUNTIME_DIR/cast.txt" | cut -c1-200)"
sleep 0.5
