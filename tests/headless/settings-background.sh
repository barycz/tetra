#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Settings > Appearance > Background:
#   two made-up pictures in the (private) Pictures folder
#   tetra-settings appearance, wheel down to the Background card
#   "Picture" tile (623,309), nothing chosen yet -> the file chooser opens
#   in Pictures; Down Return -> dusk.png
#   Fit (921,463) -> "Whole picture" (850,525)
#   report desktop.* as a notification
# Expected screenshot: the Background card with "Picture" selected, its
# path, "Whole picture"; behind the window the orange-to-purple picture
# fitted (its height), the Graphite colour at the sides; the notification
# shows "wallpaper": "picture", the picture's path and "picture_fit": "fit".
#
#   scripts/run-headless.sh -w 10 -- tests/headless/settings-background.sh
# wait: 10
set -eu

mkdir -p "$XDG_PICTURES_DIR"
make-test-picture "$XDG_PICTURES_DIR/dusk.png" 800 500 '#ff8a5c' '#5b2a86'
make-test-picture "$XDG_PICTURES_DIR/sea.png" 640 480 '#1f7a8c' '#022b3a'

tetra-settings appearance &
sleep 1.5
tetra-pointer scroll 1100 400 6
sleep 0.6
tetra-pointer click 623 309
sleep 1.5
tetra-sendkeys Down
sleep 0.3
tetra-sendkeys Return
sleep 1.5
tetra-pointer click 921 463
sleep 0.5
tetra-pointer click 850 525
sleep 1.5
tetra-pointer move 1200 300
notify-send "config.json" "$(grep -E '"(wallpaper|picture|picture_fit)"' "$TETRA_CONFIG" | tr -s ' ' | tr '\n' ' ')"
