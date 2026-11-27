#!/usr/bin/env bash
# Runs inside `tetra --greeter` at 1280x720 with a fake greetd (see
# run-headless.sh -g). Carol's account asks for a verification code after
# the password (a visible PAM prompt).
#   the user list -> Carol Danvers; the session list -> Sway
#   the password, Return       -> the field asks "Verification code:"
#   (screenshot), 123456, Return
#                              -> start_session ["sway"] with
#                                 XDG_CURRENT_DESKTOP=sway:wlroots; Sway is
#                                 remembered
# Expected screenshot: a "C" avatar, "Carol Danvers" (greyed out), an empty
# field with the hint "Verification code:", "Continue"; Session "Sway".
#
#   scripts/run-headless.sh -g -w 8 -- tests/headless/greeter-2fa.sh
# greeter: yes
# wait: 8
set -eu

failed="$XDG_RUNTIME_DIR/scenario-failed"
sleep 1.5
tetra-pointer click 640 392
sleep 0.5
tetra-pointer click 600 467
sleep 0.5
tetra-pointer click 175 683
sleep 0.5
tetra-pointer click 150 631
sleep 0.5
tetra-sendkeys --type 'tetra-test'
tetra-sendkeys Return
sleep 1
grim "$XDG_RUNTIME_DIR/last.png"
tetra-sendkeys --type '123456'
tetra-sendkeys Return
sleep 1.5
log="$XDG_RUNTIME_DIR/greetd.log"
for expected in '"username": "carol"' '"response": "123456"' '"cmd": \["sway"\]' 'XDG_CURRENT_DESKTOP=sway:wlroots"'; do
    grep -q "$expected" "$log" || echo "not in the greetd log: $expected" >>"$failed"
done
grep -q '"session": "sway"' "$XDG_STATE_HOME/tetra/greeter.json" || echo "Sway not remembered" >>"$failed"
