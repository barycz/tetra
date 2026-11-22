#!/usr/bin/env bash
# Runs inside tetra (see scripts/run-headless.sh): opens a window, rewrites the
# configuration (light theme, red accent, new background) and kills the shell.
# Expected screenshot: light title bar with red-accent border, light bar
# (restarted shell) and the new background colour.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/reload-and-restart.sh
set -eu

foot &
sleep 1.5

cat >"$TETRA_CONFIG" <<'EOF'
{
  // comments are allowed
  "theme": { "dark": false, "accent": "#e5484d" },
  "desktop": { "background": "#3b4a5e" }
}
EOF

sleep 0.5
pkill -f 'tetra-shell$' || true
wait
