#!/usr/bin/env bash
# Assemble the WebAssembly build into web/ — a static site, ready for Pages.
#
# The interface is ui/epi, copied byte for byte. Only one file is substituted:
# the page asks for juce-framework-frontend.js and gets ui/wasm/wasm-frontend.js
# with the parameter layout and the presets written above it. That is the same
# arrangement the headless host uses, and it is why there is one interface
# rather than three.
#
# Needs: emsdk on PATH (source ~/emsdk/emsdk_env.sh) and a built epi-headless,
# which is what dumps the layout and the presets out of the real instrument.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/web}"
headless="${EPI_HEADLESS_BIN:-$root/build/EpiHeadless_artefacts/Release/epi-headless}"

command -v em++ >/dev/null || { echo "build-web: em++ not on PATH — source emsdk_env.sh" >&2; exit 2; }
[[ -x "$headless" ]] || { echo "build-web: no epi-headless at $headless" >&2; exit 2; }

rm -rf "$out"; mkdir -p "$out"

echo "  compiling the engine to WebAssembly"
# Standalone: zero imports, so the AudioWorklet instantiates it with no
# Emscripten runtime at all -- there is no window and no importScripts in a
# worklet scope, and Emscripten's environment detection does not survive it.
# Memory is fixed rather than growing so the worklet can take its heap views
# once instead of every 128 frames.
em++ -std=c++20 -O3 -DNDEBUG -msimd128 -I"$root/src" \
     "$root/src/wasm/epi_wasm.cpp" "$root/src/epi/dsp/EpiEngine.cpp" \
     -sSTANDALONE_WASM=1 --no-entry \
     -sINITIAL_MEMORY=33554432 -sALLOW_MEMORY_GROWTH=0 -sSTACK_SIZE=1048576 \
     -o "$out/epi-standalone.wasm"

echo "  dumping the layout and the presets from the instrument"
"$headless" --dump-parameters > "$out/parameters.json"
"$headless" --dump-presets    > "$out/presets.json"

echo "  checking the JavaScript range conversion against juce::NormalisableRange"
"$headless" --dump-param-sweep > "$out/.sweep.json"
node "$root/tools/check-param-map.mjs" "$out/parameters.json" "$out/.sweep.json"
rm -f "$out/.sweep.json"

echo "  checking the browser's MIDI decoding against the published map"
node "$root/tools/check-midi-decode.mjs" "$out/parameters.json" | tail -2

echo "  checking the browser's preset store"
node "$root/tools/check-preset-store.mjs" "$out/parameters.json" "$out/presets.json" | tail -2

echo "  checking the MIDI file reader"
node "$root/tools/check-smf.mjs" | tail -2

echo "  assembling the interface"
cp "$root"/ui/epi/* "$out/"
cp "$root"/ui/vendor/* "$out/"
cp "$root/ui/wasm/epi-worklet.js" "$out/"

# The substitution. Data first, shim second, one file.
{
  printf 'window.EPI_VERSION_STR = %s;\n' \
    "$(node -e "console.log(JSON.stringify('v' + require('$root/tools/version.cjs')))" 2>/dev/null \
       || echo "'web'")"
  printf 'window.__EPI_PARAMS__ = '
  cat "$out/parameters.json"
  printf ';\nwindow.__EPI_PRESETS__ = '
  cat "$out/presets.json"
  printf ';\n'
  cat "$root/ui/wasm/wasm-frontend.js"
  # Web MIDI and the settings gear. Host chrome, not interface: the plugin
  # gets its MIDI from the host and the appliance from ALSA or CoreMIDI, so
  # neither of them wants a device picker. They ride in the same substituted
  # file so the page still loads exactly one extra script.
  cat "$root/ui/wasm/epi-midi.js"
  cat "$root/ui/wasm/epi-smf.js"
  cat "$root/ui/wasm/epi-player.js"
  cat "$root/ui/wasm/epi-settings.js"
} > "$out/juce-framework-frontend.js"

# parameters.json and presets.json are inlined above; they are not fetched.
rm -f "$out/parameters.json" "$out/presets.json"

# Pages serves _-prefixed paths oddly and runs Jekyll otherwise.
touch "$out/.nojekyll"

# Browsers ask for this whether or not the page mentions it, and a public site
# should not answer with a 404 in everyone's console.
printf '\x00\x00\x01\x00\x01\x00\x01\x01\x00\x00\x01\x00\x18\x00\x30\x00\x00\x00\x16\x00\x00\x00\x28\x00\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x01\x00\x18\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0e\x0e\x11\x00\x00\x00\x00\x00\x00' > "$out/favicon.ico"

echo
printf '  %-34s %s\n' "epi-standalone.wasm" "$(wc -c < "$out/epi-standalone.wasm" | tr -d ' ') bytes"
printf '  %-34s %s\n' "juce-framework-frontend.js" "$(wc -c < "$out/juce-framework-frontend.js" | tr -d ' ') bytes"
printf '  %-34s %s\n' "total" "$(du -sh "$out" | cut -f1)"
echo "  built into $out"
