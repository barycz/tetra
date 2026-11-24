#!/usr/bin/env bash
# Runs inside tetra with two 1280x720 outputs: HEADLESS-1 left, HEADLESS-2
# right. Windows open where they were, per display (windows.json in a
# private XDG_STATE_HOME):
#   foot "first" opens centred on HEADLESS-1 (frame at 291,72); drag its
#   title (500,89) to (1900,189) -> on HEADLESS-2; close it -> remembered
#   reopen it ("again") -> back on HEADLESS-2 at the same place; a second
#   foot ("other") open at the same time gets the next slot -> centred on
#   HEADLESS-1, where the pointer is
#   turn HEADLESS-2 off -> "again" moves to HEADLESS-1; on again -> back to
#   its place on HEADLESS-2 (it was there with these two displays)
#   report the remembered place, before and after, as a notification
# Expected screenshot: "other" on the left display, frame at 291,72;
# "again" on the right one, frame at 1575,144 (the window moves 116 px
# less than the pointer, as it starts moving after the first drag step; it
# was dragged partly under the bar, and opens fitted above it); the
# notification shows, before and after,
# HEADLESS-2 {"height":494,"maximized":false,"width":696,"x":296,"y":197} slots: 1
#
#   scripts/run-headless.sh -n 2 -w 10 -- tests/headless/window-memory.sh
# wait: 10
# outputs: 2
set -eu

state="$XDG_STATE_HOME/tetra/windows.json"
remembered() {
    python3 -c '
import json, sys
apps = json.load(open(sys.argv[1]))["apps"]
w = apps["memtest"]["windows"]
p = w[0]
print(p["display"], json.dumps(p["places"][p["display"]], sort_keys=True, separators=(",", ":")),
      "slots:", sum(1 for s in w if s["places"]))' "$state"
}
layout() { # $1: HEADLESS-2 enabled
    cat >"$TETRA_CONFIG" <<EOF
{ "outputs": [ { "name": "HEADLESS-1", "position": [0, 0] },
               { "name": "HEADLESS-2", "position": [1280, 0], "enabled": $1 } ] }
EOF
}

layout true
sleep 0.5
tetra-pointer move 300 300 # new windows open on the display under the pointer
foot --app-id memtest -T first &
first=$!
sleep 1.2
tetra-pointer drag 500 89 1900 189
sleep 0.5
kill "$first"
sleep 1.6 # unmapped -> remembered -> saved a second later
before="$(remembered)"

tetra-pointer move 300 300 # "other" has nothing remembered: it opens here
foot --app-id memtest -T again &
sleep 0.8
foot --app-id memtest -T other &
sleep 1.0

layout false
sleep 1.0
layout true
sleep 1.6

notify-send "windows.json" "before: $before
after: $(remembered)"
