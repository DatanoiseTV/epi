# Changelog

All notable changes to Epi are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semver](https://semver.org/) (pre-1.0: minor bumps may break).

## [Unreleased]

### Fixed
- The Clav's yarn damper defaults to mint condition: the shared DAMPER
  default meant half-aged wool and let the three-semitone release drop
  ring out at -22 dB for fifty milliseconds. At the mapped default the
  release is the brief thup the measured spectrogram shows; the bottom
  of the knob still reaches compressed old wool.
- The Clav's tone-rocker row wraps instead of clipping at the panel
  edge.

## [0.6.0] - 2026-08-22

Two new instruments, materials science, three pedals, and a measured
accuracy campaign across every model.

### Added
- **Grand** - an acoustic grand piano as the fourth instrument: 88 notes
  of one to three strings each on a fitted soundboard, radiated through a
  128-section modal stage and a spaced mic pair. Decay knees, the
  bass-left stereo image, interchannel phase, and hammer contact times
  verified row by row against measurements of a real instrument (66 rows).
- **Clav** - a tangent-action string keyboard as the fifth instrument:
  sixty strings struck and held against an anvil, twin bar pickups at
  their measured distances with a 4-way selector matrix (center, bridge,
  both, out of phase), four tone rockers as their real RC networks, the
  measured 3-semitone release drop, and a preamp calibrated to measured
  distortion points (48 rows).
- **Materials** - a MATERIAL selector on every resonator: music wire,
  stainless, bronze, brass, titanium, aluminium, tungsten, nylon. Real
  constants, not presets: geometry re-solves at the same pitch, so
  inharmonicity scales as stiffness over density (bronze halves the
  partial stretch; titanium lands on steel's curve because its ratio
  genuinely matches), internal loss adds per mode (on strings only
  through the bending share, which is why a nylon string sustains while
  a nylon rod clunks), and a bare conductor reaches a magnetic pickup
  only as the faint eddy signal its conductivity allows - aluminium
  17 dB under steel, titanium a whisper, nylon silent.
- **Three pedals** - CC64 sustain is read as a continuous value
  (half-pedalling works the way a real damper rail does, felt
  compression curve included); CC66 sostenuto latches exactly the keys
  held at the moment the pedal falls; CC67 una corda shifts the grand's
  action with the measured antisymmetric third-string growth.
- **Sympathetic resonance on every model** - the string piano's bridge
  frame (octave partner 19 dB under the struck note, twelfth 26 dB,
  non-coincident wash 38 dB - the partial-coincidence hierarchy of a
  real harp), the reed bar (proximity-coupled, subtler, as a heavy
  casting is), and the grand's pedal-open board wash.
- **Mic Studio** - a workshop bench for the grand's pair: spread widens
  the image and lowers the decorrelation onset, balance walks it,
  distance is the lid's high-band shadow, plus a level trim per mic.
  Defaults are exactly the calibrated pair.
- **Transducer matrix** - any pickup law on any resonator: magnetic,
  the instrument's native transducer, electrostatic, or contact, with
  a position control where the geometry supports one.
- **46 verified factory presets** across the five instruments, every one
  rendered and measured by its own test harness (levels, character
  claims, legal material-transducer pairings) before it ships.
- A sixth measured suite (presets) joins the other five; 350+ rows total.

### Changed
- The tagline: with an acoustic grand and a tangent keyboard aboard,
  this is a physical modeling piano, not an e-piano.
- Tine dynamics rebuilt from the action's own physics: the hammer
  ceiling grades by mass over the bass third and pianissimo pays the
  let-off gravity tax. Swing now spans the compass monotonically from
  the bass and the bass growls above the middle, as tracked on the
  real instrument.
- Reed bark recalibrated with an even-far-field capacitance law - the
  old stand-in was mathematically incapable of the measured fortissimo
  bark. A1 fortissimo harmonic energy now lands within the measured
  band, and the register response is voiced even across the compass,
  fenced by new level rows.
- String piano treble decay corrected (plain wire has no winding
  friction) and unisons now beat at constant rate rather than constant
  cents, both against the reference recordings.
- Loudness re-benched across all five instruments to within a decibel,
  with the grand deliberately a touch under (its real 22 dB attack
  crest must clear the output rail).

### Fixed
- Switching instruments now fades through silence and clears the
  outgoing chain - the old cut could replay a seconds-old ring as a
  half-scale burst, and notes played during the switch are queued into
  the incoming instrument instead of being swallowed.
- The output stage gained a physical rail (transparent below 0.85,
  bounded at 1.0): rail-limited fortissimo reed pulses were being
  carried past digital full scale by the loudness trims and hard-clipped
  in the host.
- Fast knob sweeps no longer click: the reed's electrostatic gap and
  centring and the tine's core-saturation knee now glide to their
  targets the way the voicing screw always did.
- The Clav plays through the engine exactly as its suite calibrated
  it: tone stack and preamp at the oversampled rate before the
  decimator, the drive law fitted through the calibration point, and
  the shared knob defaults landed on the voice's operating points --
  every harmonic of an A3 matches the calibrated chain within
  0.05 dB.
- The sympathetic display shows actual levels on a steady reference
  (two strings ringing 12 dB apart read 12 dB apart), instead of one
  flag color that also drifted as notes decayed.
- All-notes-off releases the sustain pedal, so stopping the transport
  cannot leave notes hanging.

## [0.5.0] - 2026-08-17

The first public release.

### Added
- Three physically modeled electric pianos behind one selector: Tine
  (cantilever rods, tone bars, magnetic pickups, opto-panner), E-Grand
  (strings on a rigid piezo bridge, mid-scooped preamp), Reed
  (solder-tuned tongues, electrostatic pickup with a real supply rail).
- 88 independent voices per instrument; damper physics on key and pedal;
  sympathetic harp coupling on the tine piano.
- Workshops: per-note length/gauge (with tuning templates: just,
  Pythagorean, meantone, Werckmeister III, quarter-tone, slendro, pelog,
  stretch, scatter — rotatable to any root), per-pickup height/gap/winding
  with manufacturing-tolerance templates, and a physical cabinet (box,
  cone, microphone distance and angle, suspension) with five voicings.
- A telemetry-driven visualizer with playable keys and true per-note swing
  amplitude and frequency.
- Factory presets for all three instruments; user presets embed the
  complete workshop state.
- CLARITY air-shelf tone control; loudness matched across instruments at
  -18 dBFS RMS; parameter smoothing throughout.
- Four measured test suites (130+ rows) run in CI on macOS, Windows and
  Linux.
