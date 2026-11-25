#!/usr/bin/env bash
# Runs inside tetra at 1280x720. tetra-terminal draws what programs print:
#   attributes, the 16 colours (normal and bright), 256-colour and
#   true-colour backgrounds, box drawing, non-ASCII text, $TERM and the size
# Expected screenshot: a "render" terminal (80x24, centred) showing bold,
# italic, underline, strike-through and reverse; a row of 16 colour
# swatches; colour0-7 and bright0-7 in their colours; a true-colour
# gradient; a box whose lines join; "äöü ß αβγ ← → ✓";
# "TERM=xterm-256color cols=80 lines=24"; the cursor below.
#
#   scripts/run-headless.sh -w 4 -- tests/headless/terminal-render.sh
# wait: 4
set -eu

demo="$XDG_RUNTIME_DIR/terminal-demo.sh"
cat >"$demo" <<'EOF'
printf '\e[1mbold\e[0m \e[3mitalic\e[0m \e[4munderline\e[0m \e[9mstrike\e[0m \e[7mreverse\e[0m\n'
for i in $(seq 0 15); do printf '\e[48;5;%sm  \e[0m' "$i"; done; echo
for i in $(seq 0 7); do printf '\e[3%sm colour%s \e[0m' "$i" "$i"; done; echo
for i in $(seq 0 7); do printf '\e[9%sm bright%s \e[0m' "$i" "$i"; done; echo
for r in 0 64 128 192 255; do printf '\e[48;2;%s;100;200m    \e[0m' "$r"; done; echo ' truecolor'
printf '┌──────┬──────┐\n│ box  │ draw │\n├──────┼──────┤\n╰──────┴──────╯\n'
printf 'äöü ß αβγ ← → ✓\n'
echo "TERM=$TERM cols=$(tput cols) lines=$(tput lines)"
sleep 30
EOF
tetra-terminal --title render bash "$demo" &
sleep 2
