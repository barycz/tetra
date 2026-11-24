#!/usr/bin/env bash
# Start tetra on the headless backend, run a client command inside it, take a
# screenshot, and shut everything down. Uses a private XDG_RUNTIME_DIR, so it
# never touches a real session.
#
# Usage: scripts/run-headless.sh [options] [--] [command...]
#   -o FILE    screenshot path        (default: build/screenshots/headless.png)
#   -s WxH     output size            (default: 1280x720)
#   -n COUNT   number of outputs      (default: 1; side by side)
#   -w SECS    wait before screenshot (default: 2)
#   -p PRESET  build preset to run    (default: debug)
#   -c FILE    tetra config file      (default: an empty config, i.e. defaults)
#   -d         verbose compositor log
#
# Example: scripts/run-headless.sh -- 'foot & foot -e htop'
# The compositor log is written next to the screenshot (*.log).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/screenshots/headless.png"
size=1280x720
outputs=1
wait_secs=2
preset=debug
config=""
debug=0

while getopts "o:s:n:w:p:c:dh" opt; do
    case "$opt" in
        o) out="$OPTARG" ;;
        s) size="$OPTARG" ;;
        n) outputs="$OPTARG" ;;
        w) wait_secs="$OPTARG" ;;
        p) preset="$OPTARG" ;;
        c) config="$OPTARG" ;;
        d) debug=1 ;;
        *) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
command="$*"

bin="$root/build/$preset/src/compositor/tetra"
if [[ ! -x "$bin" ]]; then
    echo "error: $bin not found; build with: cmake --build --preset $preset" >&2
    exit 1
fi

mkdir -p "$(dirname "$out")"
log="${out%.*}.log"
runtime="$(mktemp -d /tmp/tetra-headless.XXXXXX)" # mode 0700, as XDG requires

unset WAYLAND_DISPLAY DISPLAY
export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS="$outputs" WLR_LIBINPUT_NO_DEVICES=1
export WLR_RENDERER="${WLR_RENDERER:-gles2}"
export TETRA_OUTPUT_SIZE="$size"
export LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=$root/scripts/lsan.supp:print_suppressions=0}"
# Test helpers for the scenario scripts: built ones (tetra-sendkeys) and
# scripts (polkit-request); also the apps (tetra-settings).
export PATH="$root/build/$preset/tests/tools:$root/tests/tools:$root/build/$preset/src/apps/settings:$PATH"

# The lock screen must never check (or need) a real password: everything
# runs under pam_wrapper (it only intercepts PAM calls, i.e. the shell's),
# with a "tetra" service whose pam_matrix module accepts only
# $TETRA_TEST_PASSWORD for the current user.
export TETRA_TEST_PASSWORD="tetra-test"
pam_modules=/usr/lib/x86_64-linux-gnu/pam_wrapper
mkdir -p "$runtime/pam.d"
echo "$(id -un):$TETRA_TEST_PASSWORD:tetra" >"$runtime/passdb"
cat >"$runtime/pam.d/tetra" <<EOF
auth    required $pam_modules/pam_matrix.so passdb=$runtime/passdb
account required $pam_modules/pam_matrix.so passdb=$runtime/passdb
EOF
export LD_PRELOAD="libpam_wrapper.so${LD_PRELOAD:+:$LD_PRELOAD}"
export PAM_WRAPPER=1 PAM_WRAPPER_SERVICE_DIR="$runtime/pam.d"
# Sanitizer builds: pam_wrapper is preloaded ahead of the ASan runtime, and
# its RTLD_DEEPBIND dlopen of libpam would make ASan abort.
export PAM_WRAPPER_DISABLE_DEEPBIND=1
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}verify_asan_link_order=0"
# The polkit agent's setuid helper ignores LD_PRELOAD; use a stand-in that
# speaks its protocol and accepts the same test password.
export TETRA_POLKIT_HELPER="$root/tests/tools/fake-polkit-helper"
# Screenshots land here, not in the developer's ~/Pictures.
export XDG_PICTURES_DIR="$runtime/Pictures"

# Tests must not depend on the developer's own ~/.config/tetra. Scenarios
# may rewrite $TETRA_CONFIG while running (it reloads live).
if [[ -z "$config" ]]; then
    config="$runtime/config.json"
    echo '{}' >"$config"
fi
args=(-c "$config")
[[ -n "$command" ]] && args+=(-s "$command")
((debug)) && args+=(-d)

# A private session bus: the shell under test must not take over (or
# disturb) the services of a real session running on this machine.
dbus_info="$(dbus-daemon --session --fork --print-address=1 --print-pid=1 \
    --address="unix:path=$runtime/bus")"
export DBUS_SESSION_BUS_ADDRESS="$(sed -n 1p <<<"$dbus_info")"
dbus_pid="$(sed -n 2p <<<"$dbus_info")"
# The system bus too (brightness goes through logind), and a simulated
# sound device instead of the real PulseAudio/PipeWire server.
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
export TETRA_AUDIO=fake

"$bin" "${args[@]}" >"$log" 2>&1 &
pid=$!

cleanup() {
    kill -TERM "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    kill -TERM "$dbus_pid" 2>/dev/null || true
    # D-Bus-activated helpers (document portal, gvfs) mount FUSE file
    # systems in the runtime dir; unmount them before deleting it.
    for mount in "$runtime/doc" "$runtime/gvfs"; do
        fusermount3 -uz "$mount" 2>/dev/null || fusermount -uz "$mount" 2>/dev/null || true
    done
    rm -rf "$runtime" 2>/dev/null || true
}
trap cleanup EXIT

for _ in $(seq 50); do
    [[ -S "$runtime/wayland-0" ]] && break
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "error: tetra exited during startup; log:" >&2
        cat "$log" >&2
        exit 1
    fi
    sleep 0.1
done

sleep "$wait_secs"
if ! WAYLAND_DISPLAY=wayland-0 grim "$out"; then
    echo "error: screenshot failed; log tail:" >&2
    tail -n 20 "$log" >&2
    exit 1
fi
echo "$out"
