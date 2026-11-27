#!/usr/bin/env bash
# Runs inside `tetra --greeter` at 1280x720 with a fake greetd (see
# run-headless.sh -g).
#   the user list -> "Other user..."; type "dave", Return (on to the
#   password), the password (screenshot), Return
#                              -> greetd sends an info message first (it is
#                                 acknowledged without a response), then the
#                                 password; the session starts
# Expected screenshot: "Other user..." in the list, a "D" avatar, "dave"
# in the user name field and dots in the password field.
#
#   scripts/run-headless.sh -g -w 7 -- tests/headless/greeter-other.sh
# greeter: yes
# wait: 7
set -eu

failed="$XDG_RUNTIME_DIR/scenario-failed"
sleep 1.5
tetra-pointer click 640 392
sleep 0.5
tetra-pointer click 600 507
sleep 0.5
tetra-sendkeys --type 'dave'
tetra-sendkeys Return
sleep 0.3
tetra-sendkeys --type 'tetra-test'
sleep 0.5
grim "$XDG_RUNTIME_DIR/last.png"
tetra-sendkeys Return
sleep 1.5
log="$XDG_RUNTIME_DIR/greetd.log"
for expected in '"username": "dave"' '^{"type": "post_auth_message_response"}$' '"response": "\*\*\*"' \
    '"type": "start_session"'; do
    grep -q "$expected" "$log" || echo "not in the greetd log: $expected" >>"$failed"
done
grep -q '"user": "dave"' "$XDG_STATE_HOME/tetra/greeter.json" || echo "dave not remembered" >>"$failed"
