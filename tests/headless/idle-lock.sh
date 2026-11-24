#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Idle locking through ext-idle-notify:
#   set idle.lock_after to 2 s (the config reloads live), keep still
#   -> the shell locks after 2 s without input
# Expected screenshot: lock screen, no error, field empty.
#
#   scripts/run-headless.sh -w 6 -- tests/headless/idle-lock.sh
set -eu

sleep 1.0
python3 - "$TETRA_CONFIG" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path) as f:
    config = json.load(f)
config["idle"] = {"lock_after": 2, "screen_off_after": 0}
with open(path, "w") as f:
    json.dump(config, f)
EOF
