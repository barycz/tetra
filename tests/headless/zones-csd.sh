#!/usr/bin/env bash
# Runs inside tetra at 1280x720. A window that draws its own frame and
# shadow (GTK 4, client-side decorations) snaps exactly into a zone, and its
# popups open where it asks:
#   gnome-text-editor (its settings in memory, its files in private dirs)
#   Super+Left       -> fills zone 1 of "columns-2": 8..636 x 8..664
#   click its main menu button (573,31) -> the menu opens below the button
# Expected screenshot: the editor's rounded window filling the left half
# with an 8 px gap all round, its header bar fully visible; the main menu
# open right under the ☰ button.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/zones-csd.sh
# wait: 6
set -eu

private="$XDG_RUNTIME_DIR/gte"
mkdir -p "$private/config" "$private/data" "$private/cache" "$private/state"
tetra-pointer move 640 300
GSETTINGS_BACKEND=memory XDG_CONFIG_HOME="$private/config" XDG_DATA_HOME="$private/data" \
    XDG_CACHE_HOME="$private/cache" XDG_STATE_HOME="$private/state" \
    gnome-text-editor --standalone &
sleep 2.5
tetra-sendkeys super+Left
sleep 0.8
tetra-pointer click 573 31
sleep 0.8
