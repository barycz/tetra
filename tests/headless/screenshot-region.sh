#!/usr/bin/env bash
# Runs inside tetra at 1280x720.
#   Shift+Print, drag (300,200) -> (700,450)   -> a 400x250 picture
#   Alt+Print with foot focused                -> foot with its frame
#   Super+Shift+S, Escape                      -> cancelled, nothing saved
# Expected screenshot: foot; a "check" notification listing two sizes,
# 400x250 and the window's size, and "2 files".
#
#   scripts/run-headless.sh -w 8 -- tests/headless/screenshot-region.sh
# wait: 8
set -eu

png_size() { python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print("%dx%d" % struct.unpack(">II", d[16:24]))' "$1"; }

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys shift+Print
sleep 0.8
tetra-pointer drag 300 200 700 450
sleep 1.0
tetra-sendkeys alt+Print
sleep 1.0
tetra-sendkeys super+shift+s
sleep 0.8
tetra-sendkeys Escape
sleep 0.5
sizes=""
for f in "$XDG_PICTURES_DIR"/Screenshots/*.png; do sizes+="$(png_size "$f") "; done
count=$(ls "$XDG_PICTURES_DIR"/Screenshots/*.png | wc -l)
notify-send "check" "$count files: $sizes"
