#!/usr/bin/env bash
# Runs inside tetra: opens APP (default gnome-calculator), sweeps the pointer
# over its window 200 times and reports how much CPU the compositor and the
# app used meanwhile, to spot per-motion work.
#
#   scripts/run-headless.sh -w 25 -- "tests/bench/pointer-load.sh [APP]"
set -eu

app="${1:-gnome-calculator}"
dbus-run-session -- "$app" & # own D-Bus session: no hand-off to a running instance
sleep 3
app_pid=$(pgrep -n -x "$app" || pgrep -n -f "$app")

compositor_pid=$(pgrep -nx tetra || true)
ticks() { awk '{print $14 + $15}' "/proc/$1/stat"; }
c0=$(ticks "$compositor_pid")
a0=$(ticks "$app_pid")
t0=$(date +%s%N)

wlrctl pointer move 500 250 # into the window (pointer starts at 100,100)
for _ in $(seq 100); do
    wlrctl pointer move 40 15
    wlrctl pointer move -40 -15
done

t1=$(date +%s%N)
c1=$(ticks "$compositor_pid")
a1=$(ticks "$app_pid")
hz=$(getconf CLK_TCK)
echo "pointer-load: $app: wall $(((t1 - t0) / 1000000)) ms," \
     "compositor CPU $(((c1 - c0) * 1000 / hz)) ms, app CPU $(((a1 - a0) * 1000 / hz)) ms" >&2
