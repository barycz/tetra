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
#   -a         a private sound server (PipeWire, no hardware) with two
#              outputs and a microphone, instead of the simulated device
#   -g         the login screen (tetra --greeter) with a fake greetd and
#              made-up users. A login ends the compositor: then the result
#              is the scenario's own last screenshot ($XDG_RUNTIME_DIR/last.png)
#   -l         a fake logind (power actions only) on the private bus
# A scenario may save its own screenshot as $XDG_RUNTIME_DIR/last.png (the
# result then), and reports a failed check by writing to
# $XDG_RUNTIME_DIR/scenario-failed.
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
sound=0
greeter=0
logind=0

while getopts "o:s:n:w:p:c:agldh" opt; do
    case "$opt" in
        o) out="$OPTARG" ;;
        s) size="$OPTARG" ;;
        n) outputs="$OPTARG" ;;
        w) wait_secs="$OPTARG" ;;
        p) preset="$OPTARG" ;;
        c) config="$OPTARG" ;;
        a) sound=1 ;;
        g) greeter=1 ;;
        l) logind=1 ;;
        d) debug=1 ;;
        *) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
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
export PATH="$root/build/$preset/tests/tools:$root/tests/tools:$root/build/$preset/src/apps/settings:$root/build/$preset/src/apps/terminal:$root/build/$preset/src/apps/monitor:$PATH"

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
export XDG_STATE_HOME="$runtime/state" # window memory (windows.json)
# XDG autostart: none of the developer's or the system's entries.
mkdir -p "$runtime/config" "$runtime/xdg"
export XDG_CONFIG_HOME="$runtime/config" XDG_CONFIG_DIRS="$runtime/xdg"

# Tests must not depend on the developer's own ~/.config/tetra. Scenarios
# may rewrite $TETRA_CONFIG while running (it reloads live).
if [[ -z "$config" ]]; then
    config="$runtime/config.json"
    echo '{}' >"$config"
fi
args=(-c "$config")
[[ -n "$command" ]] && args+=(-s "$command")
((debug)) && args+=(-d)
((greeter)) && args+=(--greeter)

# A private session bus: the shell under test must not take over (or
# disturb) the services of a real session running on this machine.
#
# Services it starts on demand (xdg-desktop-portal, tetra-portal from this
# build, the permission store, ...) get this session's display and a home,
# config and data dirs of their own, so they never touch the developer's
# files. xdg-desktop-portal uses tetra's backend only.
mkdir -p "$runtime/data/dbus-1/services" "$runtime/portals" "$runtime/config" "$runtime/home"
printf '[D-BUS Service]\nName=org.freedesktop.impl.portal.desktop.tetra\nExec=%s\n' \
    "$root/build/$preset/src/portal/tetra-portal" \
    >"$runtime/data/dbus-1/services/org.freedesktop.impl.portal.desktop.tetra.service"
cp "$root/data/portal/tetra.portal" "$runtime/portals/"
# With XDG_DESKTOP_PORTAL_DIR set, the configuration is read from there too.
printf '[preferred]\ndefault=tetra\n' >"$runtime/portals/tetra-portals.conf"
dbus_info="$(env WAYLAND_DISPLAY=wayland-0 XDG_CURRENT_DESKTOP=tetra XDG_SESSION_TYPE=wayland \
    TETRA_CONFIG="$config" HOME="$runtime/home" XDG_CONFIG_HOME="$runtime/config" \
    XDG_DATA_HOME="$runtime/data" XDG_DESKTOP_PORTAL_DIR="$runtime/portals" \
    dbus-daemon --session --fork --print-address=1 --print-pid=1 \
    --address="unix:path=$runtime/bus")"
export DBUS_SESSION_BUS_ADDRESS="$(sed -n 1p <<<"$dbus_info")"
dbus_pid="$(sed -n 2p <<<"$dbus_info")"
# The system bus too (brightness goes through logind).
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
helper_pids=()
if ((logind)); then
    "$root/tests/tools/fake-logind" "$runtime/logind.log" &
    helper_pids+=($!)
    for _ in $(seq 30); do
        dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
            org.freedesktop.DBus.NameHasOwner string:org.freedesktop.login1 2>/dev/null | grep -q true && break
        sleep 0.1
    done
fi

# The login screen: a fake greetd, made-up accounts (no real names in
# screenshots) and two sessions to choose from.
if ((greeter)); then
    export GREETD_SOCK="$runtime/greetd.sock"
    export TETRA_GREETER_PASSWD="$runtime/greeter-passwd"
    cat >"$TETRA_GREETER_PASSWD" <<'EOF'
root:x:0:0:root:/root:/bin/bash
alice:x:1001:1001:Alice Liddell,,,:/home/alice:/bin/bash
bob:x:1002:1002::/home/bob:/bin/bash
carol:x:1003:1003:Carol Danvers:/home/carol:/bin/zsh
dave:x:1004:1004:Dave Bowman:/home/dave:/bin/bash
backup:x:1005:1005:Backups:/srv:/usr/sbin/nologin
EOF
    sessions="$runtime/greeter-data/wayland-sessions"
    mkdir -p "$sessions"
    printf '[Desktop Entry]\nType=Application\nName=tetra\nComment=A sleek, minimal desktop\nExec=tetra\nDesktopNames=tetra\n' \
        >"$sessions/tetra.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Sway\nComment=An i3-compatible compositor\nExec=sway\nDesktopNames=sway;wlroots\n' \
        >"$sessions/sway.desktop"
    export XDG_DATA_DIRS="$runtime/greeter-data"
    "$root/tests/tools/fake-greetd" "$GREETD_SOCK" "$runtime/greetd.log" &
    helper_pids+=($!)
    for _ in $(seq 30); do
        [[ -S "$GREETD_SOCK" ]] && break
        sleep 0.1
    done
fi

# Sound: never the real PulseAudio/PipeWire server. Either a simulated device
# inside the shell, or (-a) a private PipeWire in the runtime dir with
# virtual devices only: WirePlumber runs with every hardware monitor off,
# and all three keep their config and state in the runtime dir.
unset PULSE_SERVER PIPEWIRE_REMOTE PIPEWIRE_RUNTIME_DIR
sound_pids=()
if ((sound)); then
    unset TETRA_AUDIO
    sound_dir="$runtime/sound"
    mkdir -p "$sound_dir/state" "$sound_dir/config/wireplumber/wireplumber.conf.d"
    cat >"$sound_dir/config/wireplumber/wireplumber.conf.d/90-tetra-test.conf" <<'EOF'
wireplumber.profiles = {
  main = {
    hardware.audio = disabled
    hardware.bluetooth = disabled
    hardware.video-capture = disabled
    support.reserve-device = disabled
    support.logind = disabled
  }
}
EOF
    for server in pipewire wireplumber pipewire-pulse; do
        XDG_CONFIG_HOME="$sound_dir/config" XDG_STATE_HOME="$sound_dir/state" \
            "$server" >"$sound_dir/$server.log" 2>&1 &
        sound_pids+=($!)
        [[ $server == pipewire ]] && sleep 0.3
    done
    for _ in $(seq 50); do
        pactl info >/dev/null 2>&1 && break
        sleep 0.1
    done
    pactl load-module module-null-sink sink_name=speakers \
        sink_properties=device.description=Speakers >/dev/null
    pactl load-module module-null-sink sink_name=headphones \
        sink_properties=device.description=Headphones >/dev/null
    pactl load-module module-null-sink sink_name=microphone media.class=Audio/Source/Virtual channels=1 \
        sink_properties=device.description=Microphone >/dev/null
    # Virtual devices only take a volume while something plays (or
    # records): keep them busy with silence.
    for sink in speakers headphones; do
        pacat --raw --device="$sink" /dev/zero &
        sound_pids+=($!)
    done
    parec --raw --device=microphone >/dev/null &
    sound_pids+=($!)
    sleep 0.3
    pactl set-default-sink speakers
    pactl set-default-source microphone
    pactl set-sink-volume speakers 50%
    pactl set-sink-volume headphones 50%
    pactl set-source-volume microphone 80%
else
    export TETRA_AUDIO=fake
fi

"$bin" "${args[@]}" >"$log" 2>&1 &
pid=$!

cleanup() {
    kill -TERM "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    for ((i = ${#sound_pids[@]} - 1; i >= 0; i--)); do # streams first, PipeWire last
        kill -TERM "${sound_pids[i]}" 2>/dev/null || true
        wait "${sound_pids[i]}" 2>/dev/null || true
    done
    for helper in "${helper_pids[@]}"; do
        kill -TERM "$helper" 2>/dev/null || true
        wait "$helper" 2>/dev/null || true
    done
    for extra in greetd logind; do
        if [[ -f "$runtime/$extra.log" ]]; then
            echo "--- $extra requests:"
            cat "$runtime/$extra.log"
        fi
    done >>"$log"
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

# A scenario may take its own screenshot at the right moment ($XDG_RUNTIME_DIR/last.png).
if ! kill -0 "$pid" 2>/dev/null; then
    # The session ended on purpose: someone logged in at the login screen
    # (the greeter, and the compositor with it, exit), or logged out. The
    # scenario took its screenshot before.
    status=0
    wait "$pid" || status=$?
    echo "--- compositor exited with status $status" >>"$log"
    if ((greeter)) && ! grep -q '"start_session"' "$runtime/greetd.log" 2>/dev/null; then
        status="no session started"
    fi
    if [[ $status != 0 || ! -f "$runtime/last.png" ]]; then
        echo "error: tetra ended early (status $status); log tail:" >&2
        tail -n 20 "$log" >&2
        exit 1
    fi
elif ! WAYLAND_DISPLAY=wayland-0 grim "$out"; then
    echo "error: screenshot failed; log tail:" >&2
    tail -n 20 "$log" >&2
    exit 1
fi
if [[ -f "$runtime/last.png" ]]; then
    cp "$runtime/last.png" "$out"
fi
if [[ -s "$runtime/scenario-failed" ]]; then
    echo "error: scenario check failed:" >&2
    cat "$runtime/scenario-failed" >&2
    cat "$runtime/scenario-failed" >>"$log"
    exit 1
fi
echo "$out"
