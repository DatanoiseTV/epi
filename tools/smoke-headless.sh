#!/usr/bin/env bash
# Serve every file the interface is made of and check it arrives WHOLE.
#
# This exists because of a bug that every other test missed: juce::StreamingSocket
# ::write is a single ::send() and does not loop, so the server truncated any
# asset larger than the socket send buffer -- about 146 kB -- while sending a
# correct Content-Length above it. The interface's largest file is 2.8 MB and
# the page died on it; the next largest is 131 kB and fitted, so index.html,
# the stylesheet, every panel and both React bundles all passed.
#
# A size comparison against the sources is the whole check. It needs no audio
# device, so it runs on a CI runner.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${EPI_HEADLESS_BIN:-$root/build/EpiHeadless_artefacts/Release/epi-headless}"
port="${EPI_SMOKE_PORT:-18731}"

[[ -x "$bin" ]] || { echo "smoke-headless: no epi-headless at $bin" >&2; exit 2; }

"$bin" --port "$port" --bind 127.0.0.1 --no-audio > /tmp/epi-smoke.log 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT

for _ in $(seq 1 50); do
    curl -sf -o /dev/null "http://127.0.0.1:$port/" && break
    kill -0 "$pid" 2>/dev/null || { echo "smoke-headless: host exited" >&2; cat /tmp/epi-smoke.log >&2; exit 1; }
    sleep 0.2
done

fail=0 checked=0
for src in "$root"/ui/epi/* "$root"/ui/vendor/*; do
    name="$(basename "$src")"
    [[ -f "$src" ]] || continue
    # The one file the headless host substitutes on purpose.
    [[ "$name" == "juce-framework-frontend.js" ]] && continue

    want=$(wc -c < "$src" | tr -d ' ')
    # A truncated body makes curl exit 18, and under set -e that would abort
    # the run instead of reporting it -- which is the failure this script
    # exists to report. Keep the byte count and swallow the status.
    got=$(curl -s -o /dev/null -w '%{size_download}' "http://127.0.0.1:$port/$name" || true)
    checked=$((checked + 1))
    if [[ "$want" != "$got" ]]; then
        printf '  %-34s FAIL  want %s bytes, got %s\n' "$name" "$want" "$got"
        fail=$((fail + 1))
    else
        printf '  %-34s ok    %s bytes\n' "$name" "$want"
    fi
done

# The substituted one is generated, so only its tail is fixed; check it is
# served whole by looking for the last thing in the file.
sub=$(curl -s "http://127.0.0.1:$port/juce-framework-frontend.js" || true)
if [[ "$sub" != *"global.juce = global.Juce;"* ]]; then
    echo "  juce-framework-frontend.js         FAIL  truncated or not substituted"
    fail=$((fail + 1))
fi
checked=$((checked + 1))

kill "$pid" 2>/dev/null || true
echo
if (( fail )); then
    echo "smoke-headless: $fail of $checked assets did not arrive whole" >&2
    exit 1
fi
echo "smoke-headless: all $checked assets arrive whole"
