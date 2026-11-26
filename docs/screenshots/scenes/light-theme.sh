#!/usr/bin/env bash
# The light theme with an orange accent (theme.dark, theme.accent), applied
# live to the bar, the decorations and every tetra app.
# wait: 5
set -eu
echo '{ "theme": { "dark": false, "accent": "#e66100" }, "desktop": { "background": "#d8dde4" } }' >"$TETRA_CONFIG"
sleep 0.5
tetra-settings appearance &
sleep 2.5
