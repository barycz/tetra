#!/usr/bin/env bash
# Start tetra on the headless backend, run a client command inside it, take a
# screenshot, and shut everything down. Uses a private XDG_RUNTIME_DIR, so it
# never touches a real session.
#
# Usage: scripts/run-headless.sh [options] [--] [command...]
#   -o FILE    screenshot path        (default: build/screenshots/headless.png)
#   -s WxH     output size            (default: 1280x720)
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
wait_secs=2
preset=debug
config=""
debug=0

while getopts "o:s:w:p:c:dh" opt; do
    case "$opt" in
        o) out="$OPTARG" ;;
        s) size="$OPTARG" ;;
        w) wait_secs="$OPTARG" ;;
        p) preset="$OPTARG" ;;
        c) config="$OPTARG" ;;
        d) debug=1 ;;
        *) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
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
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1
export WLR_RENDERER="${WLR_RENDERER:-gles2}"
export TETRA_OUTPUT_SIZE="$size"
export LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=$root/scripts/lsan.supp:print_suppressions=0}"
# Test helpers (tetra-sendkeys) for the scenario scripts.
export PATH="$root/build/$preset/tests/tools:$PATH"

# Tests must not depend on the developer's own ~/.config/tetra.
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
