#!/usr/bin/env bash
# Runs every scenario in tests/headless/ and reports crashes and sanitizer
# findings. Screenshots and logs go to build/screenshots/<preset>/.
#
# Usage: scripts/run-tests.sh [preset]     (default: asan)
# The screenshots still need a human (or Claude) to look at them.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-asan}"
out="$root/build/screenshots/$preset"
mkdir -p "$out"

cmake --build --preset "$preset" >/dev/null || { echo "build failed" >&2; exit 1; }

failed=0
for test in "$root"/tests/headless/*.sh; do
    name="$(basename "$test" .sh)"
    # A scenario may ask for a longer wait with a "# wait: SECS" header line.
    wait="$(sed -n 's/^# wait: *\([0-9.]*\)$/\1/p' "$test" | head -n 1)"
    "$root/scripts/run-headless.sh" -p "$preset" -w "${wait:-6}" -o "$out/$name.png" -- "$test" >/dev/null 2>&1
    status=$?
    log="$out/$name.log"
    problems=$(grep -cE "ERROR: (Address|Leak|UndefinedBehavior)Sanitizer|runtime error:|Assertion .* failed|Segmentation fault" "$log")
    if [[ $status -ne 0 || $problems -ne 0 ]]; then
        echo "FAIL  $name (exit $status, $problems problems) - see $log"
        failed=1
    else
        echo "ok    $name"
    fi
done
exit $failed
