#!/usr/bin/env bash
# Runs inside tetra: switches to a light theme with a different font size and
# accent while the shell keeps running, so the shell must re-apply its theme
# (including rebuilding ImGui fonts) live.
# Expected screenshot: light bar, green logo, larger clock text.
#
#   scripts/run-headless.sh -w 4 -- tests/headless/live-theme.sh
set -eu

sleep 1.0
cat >"$TETRA_CONFIG" <<'EOF'
{ "theme": { "dark": false, "accent": "#2f9e6b", "font_size": 17 } }
EOF
sleep 1.0
# A broken file must be rejected; the previous configuration stays.
echo '{ "theme": { "font_size": "huge" } }' >"$TETRA_CONFIG"
