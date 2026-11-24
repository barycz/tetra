#!/usr/bin/env bash
# Runs inside tetra at 1280x720; screenshots go to $XDG_PICTURES_DIR (a
# private directory, see run-headless.sh).
#   foot, then Print       -> the whole output is saved and copied
#   check the file and the clipboard, and report as a notification
# Expected screenshot: foot; a "Screenshot taken" notification with a
# thumbnail; a "check" notification: "1 file, 1280x720, clipboard image/png
# <size> bytes" (the same PNG size twice).
#
#   scripts/run-headless.sh -w 6 -- tests/headless/screenshot-output.sh
set -eu

png_size() { python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print("%dx%d" % struct.unpack(">II", d[16:24]))' "$1"; }

sleep 1.0
foot &
sleep 1.2
tetra-sendkeys Print
sleep 1.5
dir="$XDG_PICTURES_DIR/Screenshots"
files=("$dir"/*.png)
clip="$(timeout 3 wl-paste --list-types 2>&1 | tr '\n' ' ')"
bytes="$(timeout 3 wl-paste --type image/png 2>/dev/null | wc -c)"
notify-send "check" "${#files[@]} file, $(png_size "${files[0]}"), clipboard ${clip}${bytes} bytes, file $(stat -c %s "${files[0]}") bytes"
