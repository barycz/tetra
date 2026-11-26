#!/usr/bin/env bash
# tetra-terminal: colours, text attributes, box drawing, Unicode.
# wait: 4
set -eu
demo="$XDG_RUNTIME_DIR/demo.sh"
cat >"$demo" <<'DEMO'
printf '\e[1mbold\e[0m \e[3mitalic\e[0m \e[4munderline\e[0m \e[9mstrike\e[0m \e[7mreverse\e[0m\n\n'
for i in $(seq 0 7); do printf '\e[3%sm colour%s \e[0m' "$i" "$i"; done; echo
for i in $(seq 0 7); do printf '\e[9%sm bright%s \e[0m' "$i" "$i"; done; echo; echo
for i in $(seq 0 35); do printf '\e[48;2;%s;%s;220m  \e[0m' $((i * 7)) $((200 - i * 4)); done; echo; echo
printf '┌──────────┬──────────┐\n│ tetra    │ terminal │\n├──────────┼──────────┤\n╰──────────┴──────────╯\n\n'
printf 'äöü ß αβγ ← → ✓  ▁▂▃▄▅▆▇█\n'
sleep 30
DEMO
tetra-terminal --title terminal bash "$demo" &
sleep 2
