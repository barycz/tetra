#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Ending the shell must close tetra-terminal
# (the PTY may report only a hang-up, without data; the terminal used to
# miss that and spin at full CPU with "exit" on the screen):
#   5 times: a terminal with bash, Ctrl+D -> the terminal is gone within 2 s
#   once: "exit 3" typed -> the terminal is gone, with exit status 3
# Expected screenshot: no windows; a notification "terminal-exit" saying
# "Ctrl+D closed 5 of 5; exit 3 -> status 3".
#
#   scripts/run-headless.sh -w 20 -- tests/headless/terminal-exit.sh
# wait: 20
set -eu

# Waits up to 2 s for the terminal to end and sets $result to its exit
# status, or "stuck" (then kills it). Not run in a subshell: only this
# shell can wait for its children.
ended() {
    local pid=$1
    for _ in $(seq 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid"
        wait "$pid" 2>/dev/null || true
        result=stuck
        return
    fi
    result=0
    wait "$pid" || result=$?
}

closed=0
for i in 1 2 3 4 5; do
    tetra-terminal --title "exit $i" env PS1='$ ' HISTFILE=/dev/null bash --norc --noprofile &
    pid=$!
    sleep 1.2
    tetra-sendkeys ctrl+d
    ended "$pid"
    [[ "$result" == stuck ]] || closed=$((closed + 1))
done

tetra-terminal --title "exit 3" env PS1='$ ' HISTFILE=/dev/null bash --norc --noprofile &
pid=$!
sleep 1.2
tetra-sendkeys --type $'exit 3\n'
ended "$pid"

notify-send -t 0 "terminal-exit" "Ctrl+D closed $closed of 5; exit 3 -> status $result"
