#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Typing into tetra-terminal (bash without
# rc files), then the scrollback:
#   type a printf with single and double quotes -> printed back
#   seq 1 200; then 10 wheel notches up over the window -> 30 lines back
# Expected screenshot: the "input" terminal scrolled back 30 lines, so the
# numbers end at 171 (no prompt or cursor at the bottom), and a scroll
# indicator at the right edge.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/terminal-input.sh
# wait: 6
set -eu

tetra-terminal --title input env PS1='$ ' HISTFILE=/dev/null bash --norc --noprofile &
sleep 2
command=$(cat <<'EOF'
printf '%s\n' "it's" 'say "hi"' $((6 * 7))
EOF
)
tetra-sendkeys --type "$command"$'\n'
sleep 0.5
tetra-sendkeys --type $'seq 1 200\n'
sleep 0.8
tetra-pointer scroll 640 400 -10
sleep 0.5
