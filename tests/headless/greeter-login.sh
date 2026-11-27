#!/usr/bin/env bash
# Runs inside `tetra --greeter` at 1280x720 with a fake greetd (see
# run-headless.sh -g): Alice Liddell is preselected.
#   a wrong password, Return   -> "Wrong password", greetd told to cancel
#   (screenshot), the right password, Return
#                              -> start_session ["tetra"] with
#                                 XDG_CURRENT_DESKTOP=tetra; the greeter and
#                                 the compositor exit; alice is remembered
# Expected screenshot (taken before the second try): the login screen with
# "Wrong password" in red under "Log in" and an empty password field.
#
#   scripts/run-headless.sh -g -w 7 -- tests/headless/greeter-login.sh
# greeter: yes
# wait: 7
set -eu

failed="$XDG_RUNTIME_DIR/scenario-failed"
sleep 1.5
tetra-sendkeys --type 'not-it'
tetra-sendkeys Return
sleep 1
grim "$XDG_RUNTIME_DIR/last.png"
tetra-sendkeys --type 'tetra-test'
tetra-sendkeys Return
sleep 1.5
log="$XDG_RUNTIME_DIR/greetd.log"
for expected in '"username": "alice"' '"response": "WRONG not-it"' '"type": "cancel_session"' '"response": "\*\*\*"' \
    '"cmd": \["tetra"\]' 'XDG_CURRENT_DESKTOP=tetra"' 'XDG_SESSION_DESKTOP=tetra"'; do
    grep -q "$expected" "$log" || echo "not in the greetd log: $expected" >>"$failed"
done
grep -q '"user": "alice"' "$XDG_STATE_HOME/tetra/greeter.json" || echo "alice not remembered" >>"$failed"
