#!/usr/bin/env bash
# Regenerate the table in docs/ControlMap.md from the instrument itself.
#
# The names, ranges and defaults come from the live parameter layout and the
# numbers from src/epi/ControlMap.h, so the document cannot drift from what the
# instrument actually answers. Run this after changing either. With --check it
# reports drift instead of fixing it, which is what CI runs.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
doc="$root/docs/ControlMap.md"
bin="${EPI_HEADLESS_BIN:-$root/build/EpiHeadless_artefacts/Release/epi-headless}"

if [[ ! -x "$bin" ]]; then
    echo "update-control-map: no epi-headless at $bin" >&2
    echo "  build it with: cmake --build build --target EpiHeadless" >&2
    exit 2
fi

begin='<!-- BEGIN GENERATED — do not edit by hand; run tools/update-control-map.sh -->'
end='<!-- END GENERATED -->'

tmp="$(mktemp)"
trap 'rm -f "$tmp" "$tmp.table"' EXIT
"$bin" --dump-control-map > "$tmp.table"

awk -v begin="$begin" -v end="$end" -v table="$tmp.table" '
    $0 == begin { print; while ((getline line < table) > 0) print line; skip = 1; next }
    $0 == end   { print; skip = 0; next }
    !skip       { print }
' "$doc" > "$tmp"

if [[ "${1:-}" == "--check" ]]; then
    if diff -q "$doc" "$tmp" > /dev/null; then
        echo "docs/ControlMap.md is up to date"
    else
        echo "docs/ControlMap.md is stale — run tools/update-control-map.sh" >&2
        diff -u "$doc" "$tmp" | head -40 >&2
        exit 1
    fi
else
    cp "$tmp" "$doc"
    echo "docs/ControlMap.md updated"
fi
