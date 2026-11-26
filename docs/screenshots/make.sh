#!/usr/bin/env bash
# Regenerates the pictures in docs/screenshots/ from scenes/*.sh, each in
# its own headless tetra session (1280x720, private runtime dir, empty
# config, see scripts/run-headless.sh).
#
#   docs/screenshots/make.sh [-p PRESET] [SCENE...]   (default: release, all)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
preset=release
if [[ "${1:-}" == -p ]]; then
    preset="$2"
    shift 2
fi
scenes=("$@")
if ((${#scenes[@]} == 0)); then
    for scene in "$here"/scenes/*.sh; do
        scenes+=("$(basename "$scene" .sh)")
    done
fi

work="$root/build/screenshots/gallery"
mkdir -p "$work"
for name in "${scenes[@]}"; do
    scene="$here/scenes/$name.sh"
    wait="$(sed -n 's/^# wait: *\([0-9.]*\)$/\1/p' "$scene" | head -n 1)"
    if "$root/scripts/run-headless.sh" -p "$preset" -w "${wait:-4}" -o "$work/$name.png" -- "$scene" >/dev/null; then
        cp "$work/$name.png" "$here/$name.png"
        echo "ok    $name"
    else
        echo "FAIL  $name - see $work/$name.log"
    fi
done
