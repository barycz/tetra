#!/usr/bin/env bash
# Runs inside tetra at 1280x720. The desktop background follows the config
# live (no windows open):
#   the logo (default) -> a picture, filled -> the logo with a green accent
#   on Navy -> a missing picture (the colour stays) -> a small picture at
#   its own size on Plum -> the same picture as a JPEG stored sideways
#   with EXIF orientation 6, shown whole on a near-black colour
# Expected screenshot: the teal-to-dark-blue picture upright (light disc at
# the upper right, as in sea.png) filling the height of the screen
# (960x720), near-black bars at the sides.
#
#   scripts/run-headless.sh -w 10 -- tests/headless/desktop-background.sh
# wait: 10
set -eu

mkdir -p "$XDG_PICTURES_DIR"
make-test-picture "$XDG_PICTURES_DIR/dusk.png" 800 500 '#ff8a5c' '#5b2a86'
make-test-picture "$XDG_PICTURES_DIR/sea.png" 640 480 '#1f7a8c' '#022b3a'

sleep 0.8
cat >"$TETRA_CONFIG" <<EOF
{ "desktop": { "wallpaper": "picture", "picture": "$XDG_PICTURES_DIR/dusk.png" } }
EOF
sleep 1.2
cat >"$TETRA_CONFIG" <<EOF
{ "theme": { "accent": "#3fb950" }, "desktop": { "background": "#18243a" } }
EOF
sleep 1.0
cat >"$TETRA_CONFIG" <<EOF
{ "desktop": { "wallpaper": "picture", "picture": "$XDG_PICTURES_DIR/nothing-here.png" } }
EOF
sleep 1.0
cat >"$TETRA_CONFIG" <<EOF
{ "desktop": { "wallpaper": "picture", "picture": "$XDG_PICTURES_DIR/sea.png",
               "picture_fit": "center", "background": "#2a1f33" } }
EOF
sleep 1.5
tetra-png-to-jpeg "$XDG_PICTURES_DIR/sea.png" "$XDG_PICTURES_DIR/sea.jpg" 6
cat >"$TETRA_CONFIG" <<EOF
{ "desktop": { "wallpaper": "picture", "picture": "$XDG_PICTURES_DIR/sea.jpg",
               "picture_fit": "fit", "background": "#0b0d10" } }
EOF
sleep 1.5
