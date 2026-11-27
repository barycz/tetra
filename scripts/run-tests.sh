#!/usr/bin/env bash
# Runs the unit tests and every scenario in tests/headless/, and reports
# crashes and sanitizer findings. Screenshots and logs go to build/screenshots/<preset>/.
#
# Usage: scripts/run-tests.sh [preset [pattern]]   (default: asan, every scenario)
#        scripts/run-tests.sh installed           the installed package (the test
#                                                 helpers come from the debug build)
# pattern: a glob of scenario names, e.g. 'lock-*'
# The screenshots still need a human (or Claude) to look at them.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-asan}"
pattern="${2:-*}"
out="$root/build/screenshots/$preset"
mkdir -p "$out"
run=()
if [[ "$preset" == installed ]]; then
    preset=debug # for its test helpers
    run=(-i)
fi

cmake --build --preset "$preset" >/dev/null || { echo "build failed" >&2; exit 1; }

failed=0
if ((${#run[@]})); then
    : # the package build ran the unit tests
elif "$root/build/$preset/tests/unit/tetra-unit-tests" >"$out/unit.log" 2>&1; then
    echo "ok    unit tests"
else
    echo "FAIL  unit tests - see $out/unit.log"
    failed=1
fi
for test in "$root"/tests/headless/$pattern.sh; do
    name="$(basename "$test" .sh)"
    # A scenario may ask for a longer wait with a "# wait: SECS" header line,
    # for more displays with "# outputs: N", and for a private sound server
    # with "# audio: pipewire".
    wait="$(sed -n 's/^# wait: *\([0-9.]*\)$/\1/p' "$test" | head -n 1)"
    outputs="$(sed -n 's/^# outputs: *\([0-9]*\)$/\1/p' "$test" | head -n 1)"
    # "# greeter: yes" runs the login screen with a fake greetd, "# logind: fake"
    # a fake logind.
    extra=()
    grep -q '^# audio: pipewire$' "$test" && extra+=(-a)
    grep -q '^# greeter: yes$' "$test" && extra+=(-g)
    grep -q '^# logind: fake$' "$test" && extra+=(-l)
    # "# env: NAME=VALUE" lines go into the session's environment.
    mapfile -t envs < <(sed -n 's/^# env: *\([A-Z_][A-Z0-9_]*=.*\)$/\1/p' "$test")
    env "${envs[@]}" "$root/scripts/run-headless.sh" "${run[@]}" -p "$preset" -w "${wait:-6}" -n "${outputs:-1}" "${extra[@]}" -o "$out/$name.png" \
        -- "$test" >/dev/null 2>&1
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
