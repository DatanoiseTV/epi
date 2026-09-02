<div align="center">

# Epi

### Five instruments, built from physics — no samples anywhere

**Tine · E-Grand · Reed · Grand · Clav** — VST3 · AU · CLAP · Standalone

![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%C2%B7%20AU%20%C2%B7%20CLAP%20%C2%B7%20Standalone-caa45e)
![Platforms](https://img.shields.io/badge/platforms-macOS%20%C2%B7%20Windows%20%C2%B7%20Linux-555)

<img src="docs/img/epi-tine.png" width="100%" alt="Epi main panel" />

</div>

## What this is

Epi computes every note from the physics of the instrument: a hammer strikes
a piece of steel, the steel rings, and a transducer — or, on the acoustic
grand, a pair of microphones — turns its motion into a signal. There is no
sample library and no oscillator standing in for one — when you change the
hammer, the pickup, or the steel itself, the sound changes the way it would
on the real bench.

Five instruments live behind one selector:

- **Tine** — the classic tine piano: 88 tuned steel rods with tone bars,
  magnetic pickups, and the stereo panner its amplifier called vibrato. The
  harmonics come from the pickup's field, not the metal — which is why the
  pickup HEIGHT knob re-voices the instrument the way the real voicing screw
  does, and why playing harder growls instead of just getting louder.
- **E-Grand** — the electric stage grand: real strings on a rigid bridge
  with a piezo pickup that reads string force, a mid-scooped preamp, and the
  long singing sustain that made these tour.
- **Reed** — the reed piano: solder-tuned steel tongues over an
  electrostatic pickup polarised at 150 volts. The louder you play, the more
  the gap's asymmetry barks — that snarl is the geometry of the pickup, and
  the SUPPLY knob is a real voltage.
- **Grand** — an acoustic grand: 88 notes of one to three strings each on a
  fitted soundboard, radiated through a spaced mic pair. Decay knees, the
  bass-left stereo image, half-pedal and the pedal-open board wash all come
  out of the same coupled model, verified row by row against measurements
  of a real instrument.
- **Clav** — a tangent-action string keyboard: sixty strings struck and
  held against an anvil, twin bar pickups at their measured distances with
  a 4-way selector (center, bridge, both, out of phase), four tone rockers
  computed as the real RC networks behind them, and the measured
  three-semitone pitch drop when the tangent lets go and the yarn-wrapped
  dead length rejoins the string.

<div align="center">
<img src="docs/img/epi-grand.png" width="100%" alt="The acoustic grand: soundboard and mic pair" />
</div>

Every voice is its own mechanism. All 88 notes have their own hammer, their
own resonator, their own pickup — chords never steal voices and repeated
notes meet steel that is already moving. Sympathetic resonance runs on
every model, with the partial-coincidence hierarchy of a real harp: on the
string piano a struck note rings its octave partner 19 dB down, the
twelfth 26 dB down, and the non-coincident wash 38 dB down.

<div align="center">
<img src="docs/img/epi-clav.png" width="100%" alt="Clav: tangent strings, twin bar pickups, tone rockers" />
</div>

## Materials

Every resonator has a MATERIAL selector: music wire, stainless, bronze,
brass, titanium, aluminium, tungsten, nylon. These are real material
constants, not tone presets. The geometry re-solves at the same pitch, so
inharmonicity scales as stiffness over density — bronze halves the partial
stretch, and titanium lands on steel's curve because its ratio genuinely
matches. Internal loss enters per mode, and on strings only through the
bending share — which is why a nylon string sustains while a nylon rod
clunks. A bare conductor reaches a magnetic pickup only as the faint eddy
signal its conductivity allows: aluminium sits 17 dB under steel, titanium
is a whisper, and nylon — an insulator — is silent, exactly where it must be.

The transducer is swappable too: magnetic, the instrument's native
transducer, electrostatic, or contact, on any resonator, with a position
control wherever the geometry supports one. The factory presets are tested
against the pairing rules, so no shipped sound is a silent
material-transducer combination.

## Pedals

- **Sustain (CC64)** is read as a continuous value, not a switch: the
  damper felt compresses the way a real damper rail does, so half-pedalling
  works, with the felt-compression curve included.
- **Sostenuto (CC66)** latches exactly the keys held at the moment the
  pedal falls (the grand — the electrics never had a middle pedal).
- **Una corda (CC67)** shifts the grand's action so the hammer meets two
  strings of a trichord and one of a bichord, and monochords meet fresh,
  softer felt.

## The workshops

The panel gets you the classic sounds. The workshops let you rebuild the
instrument.

<div align="center">
<img src="docs/img/epi-workshop.png" width="100%" alt="Tine workshop with a just intonation template" />
</div>

- **Tine / String workshop** — re-cut any note's steel. The LENGTH lane
  retunes by the beam and string equations (paint microtonal scales by hand,
  or apply just intonation, Pythagorean, quarter-comma meantone,
  Werckmeister III, slendro, pelog, an octave stretch, or a barroom scatter
  with one click, rotated to any root). The GAUGE lane swaps the wire:
  pitch stands, and the overtone character moves — fat wire turns the tine
  bank into gongs and the grand's strings bell-like.
- **Pickup workshop** — every pickup gets its own height, gap, and winding.
  Three tolerance templates paint the manufacturing scatter of a well-kept,
  worn, or neglected instrument — deterministic, so your instrument is the
  same one every session.
- **Cabinet workshop** — a speaker with dimensions instead of an impulse
  response: box volume sets the resonance, cone size sets the breakup, the
  microphone has distance and angle, and the suspension decides when it
  grinds. Five cabinets ship as one-click starting points.
- **Mic Studio** — the grand's bench, in two modes. Classic Pair: spread
  widens the calibrated pair and deepens the bass-left image, balance
  walks it, distance is the lid's high-band shadow, each mic has its own
  trim. Stage: up to five microphones dragged freely on a top-down view
  of the instrument, each rendered from real geometry — inverse-distance
  level (6 dB per doubling, measured), arrival delay at the speed of
  sound, the board's dipole (a mic under the board reads the low band
  inverted), and the lid as a specular image that brightens the open
  side. The whole five-mic stage costs about 2% of a core.

<div align="center">
<img src="docs/img/epi-mic-studio.png" width="100%" alt="Mic Studio: the grand's spaced pair on the bench" />
</div>

Everything the workshops hold is saved in your project **and inside every
preset you save** — a saved sound is the whole sound. Factory presets ship
across all five instruments, and every one of them is rendered and measured
by its own test harness (levels, character claims, legal
material-transducer pairings) before it ships.

## Playing it

- Click the drawn keys (deeper on the key is louder) or play A–; on your
  computer keyboard — or just send it MIDI, including the three pedals
  (CC64/66/67), expression (CC11), and pitch bend.
- The visualizer is telemetry, not animation: every rod swings at its true
  frequency by the measured amount, hammers fire when the engine strikes,
  and a SUSTAIN lamp shows the pedal state your keyboard is actually
  sending.
- BASS, TREBLE, and CLARITY are the channel strip; DRIVE and CORE SAT (or
  SUPPLY, on the reed piano) are where the dirt lives, because that is
  where it lives on the instruments.
- ROOM and SIZE place the output in a space, and SPACE picks which one:
  the adjustable studio room the plugin shipped with, or one of five
  surveyed profiles — booth, studio, stage, hall, church — each with its
  decay computed per octave band from published absorption data and its
  early reflections from the room's actual geometry. The size knob scales
  the surveyed room's dimensions and the physics follows.
- The five instruments are level-matched to within a decibel at a shared
  -18 dBFS mezzo-forte bench — except the grand, deliberately about 6 dB
  under, because its real 22 dB attack crest must clear the output rail
  (transparent below 0.76, bounded a decibel under full scale). Switching
  instruments fades through silence, and fast knob sweeps glide instead
  of clicking.

## Install

Grab the zip for your platform from
[Releases](../../releases), unzip, and copy the plugin bundles to your
plugin folder (an INSTALL.txt with the exact paths is inside each zip).

macOS builds are unsigned: right-click → Open the standalone once, or run
`xattr -cr` on the bundles, and your DAW will load them.

Formats by platform, exactly as CI ships them:

| | VST3 | AU | CLAP | Standalone |
|---|---|---|---|---|
| macOS (universal) | yes | yes | yes | yes |
| Windows (x64) | yes | – | yes | yes |
| Linux (x64) | yes | – | yes | yes |

Building from source needs CMake ≥ 3.22 and a C++20 compiler; JUCE is a
submodule. `cmake -S . -B build && cmake --build build --target Epi_All`.

## Running it without a computer screen

`epi-headless` is the same instrument with no plugin host and no window. It
opens an audio device and MIDI ports itself and serves the interface over
HTTP, so a phone or a laptop on the same network is the front panel. It is
meant for a small computer inside an instrument, a rack box, or a Pi on a
stage. Linux and macOS; it ships in the Linux and macOS zips.

```
epi-headless --list-devices
epi-headless --device "USB Audio" --port 8080 --preset "Suitcase"
```

Then open `http://<the machine>:8080/` — that is the plugin's own interface,
served byte for byte. `--bind 127.0.0.1` refuses everything but the machine
itself; `--midi-in` and `--midi-out` pick ports; `--help` lists the rest.

It hosts the same processor the plugin does, so presets, the workshops and
saved state are the plugin's — a preset saved on the appliance loads in the
plugin.

It is a console program, not a windowed one, and that is the point: it
pulls in no browser engine, so on Linux it needs no GTK or WebKit headers
to build. Measured on the built binary, x64 and arm64 alike, the only
libraries it needs at runtime are **libasound2** and **libfreetype6** — no
X11, no GTK, no WebKit — and it runs with `DISPLAY` unset. That is what
makes a minimal Raspberry Pi OS Lite image enough.

Whether a given board keeps up is a separate question and depends on the
board: start it, play, and watch for dropouts before trusting it on stage.

### Building a physical panel

Every one of the 49 parameters is reachable over MIDI as a CC and as an
NRPN, and — this is the part that makes hardware practical — Epi reports
every change back. Load a preset from the web interface and the encoders'
displays follow, instead of showing whatever they were left at.

CC is one message and 128 steps, for a generic controller or a sequencer
lane. NRPN is 16384 steps, finer than the interface's own knobs resolve, so
an encoder can sweep `pickupPos` or `coilFreq` without stepping audibly.
Where a controller number has a real meaning in the MIDI specification and
it matches the parameter, the standard number is used: CC 7 is the output
level, CC 74 the treble, CC 92 the tremolo depth, CC 91 the space.

```
epi-headless --midi-in "My Panel" --midi-out "My Panel"
```

[docs/ControlMap.md](docs/ControlMap.md) is the map: every number, the
message formats worked through, what gets reported back and when, and the
controllers Epi deliberately leaves alone. Those numbers are pinned by a
test, so they can move only as a decision — hardware built against them
keeps working.

## Playing it in a browser

The whole instrument also compiles to WebAssembly, so it runs in a browser tab
with nothing installed and no server behind it — the DSP on your machine, out
of your speakers. Same engine, same interface, same presets.

Build it yourself with [emsdk](https://emscripten.org/) on PATH:

```
cmake --build build --target EpiHeadless   # dumps the layout and the presets
./tools/build-web.sh                       # assembles web/
```

`web/` is a static site: 360 kB of WebAssembly and about 3.7 MB in total, most
of which is the JSX transformer.

It is the same `ui/epi` bundle the plugin ships, copied byte for byte. Only one
file differs — the one that talks to the host — which is the same arrangement
the headless build uses. Three hosts, one interface.

A gear in the corner opens MIDI settings: which inputs are listening, which
channel, what is arriving right now, where the pedals are, and the whole
controller map with per-parameter MIDI learn. It answers the same CC and NRPN
numbers as the hardware build — one published map, three hosts — so a
controller template written against
[docs/ControlMap.md](docs/ControlMap.md) drives all of them. Learned bindings
are remembered in the browser.

Web MIDI is Chrome, Edge and Firefox; Safari does not implement it, and the
panel says so rather than looking broken.

**Drop a `.mid` on the page**, or press **MIDI File**, and it plays on the
instrument with a transport at the bottom of the window. Both file layouts work — format 0, where one
track holds every channel, and format 1, where the tracks are simultaneous.

The piano parts are picked automatically by scoring each channel of each track
on its program number, its name and its range; General MIDI channel 10 is never
one. The choice is always shown and always overridable, and when the evidence
is thin the part list opens rather than quietly playing something the file did
not ask for.

The score is handed to the audio thread and played from its own sample clock,
not fired from a timer — so every note lands on the sample the file asks for,
rather than on whichever 2.7 ms block boundary a timer happened to catch. The on-screen keyboard and your
computer keyboard work everywhere.

Presets work the way you would expect. Save from the preset browser and it is
kept in your browser; the settings panel exports any preset — or the whole
bank — as a file you can keep, move to another machine, or send to someone,
and import one back. Closing the tab is not the same as throwing the
instrument away: the last session comes back on reload, including edits you
never saved. Everything is stored locally and nothing is sent anywhere;
"Forget everything" in the settings clears it.

One limit worth stating plainly: held chords with the pedal down are the
expensive case, about 1.4× slower than native — 19% of a core for ten notes
and half a core for forty — so a phone or an older laptop will glitch at the
extreme end.

## How honest is "physical"?

Measured, not asserted. The models are calibrated against recordings of the
real instruments and against the published measurements of the people who
put them under high-speed cameras and spectrum analysers — and the repo
carries the receipts: eight test suites of numbered rows render audio
offline and measure it, from inharmonicity curves and per-partial decay
rates to "a chord must equal the sum of its notes on the piezo bridge, and
must NOT on a coupled soundboard". One suite per model family (tine, reed,
grand, clav), one for the DSP cores, one that renders every factory preset
and holds it to its own claims, one for the engine's seams (instrument
switching, pedals, knob-sweep clicks, the output rail), and one that
round-trips plugin state through the real processor — all run in CI on
macOS, Windows, and Linux. `docs/` holds the implementation plans and the research notes
with every number's provenance.

## Trademarks

Fender, Rhodes, Wurlitzer, Yamaha, and Hohner are trademarks of their
respective owners. Epi is not affiliated with, endorsed by, or sponsored by
any of them; their names appear only in the documentation, factually, to
identify which instruments were measured.

## License

GPL-3.0. The DSP is plain C++ headers with the physics explained in the
comments — if you want to know why a knob does what it does, the answer is
in the file, usually with the measurement that decided it.
