# Changelog

All notable changes to Didge are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semver](https://semver.org/) (pre-1.0: minor bumps may break).

## Epi [0.5.0] - 2026-08-17

The first public release of Epi, the second instrument family in this
repository.

### Added
- Three physically modeled electric pianos behind one selector: Tine
  (cantilever rods, tone bars, magnetic pickups, opto-panner), E-Grand
  (strings on a rigid piezo bridge, mid-scooped preamp), Reed
  (solder-tuned tongues, electrostatic pickup with a real supply rail).
- 88 independent voices per instrument; sustain, sostenuto-free damper
  physics, sympathetic harp coupling on the tine piano.
- Workshops: per-note length/gauge (with tuning templates: just,
  Pythagorean, meantone, Werckmeister III, quarter-tone, slendro, pelog,
  stretch, scatter — rotatable to any root), per-pickup height/gap/winding
  with tolerance templates, and a physical cabinet (box, cone, microphone
  distance and angle, suspension) with five voicings.
- A telemetry-driven visualizer with playable keys and true per-note swing
  amplitude and frequency.
- Factory presets for all three instruments; user presets embed the
  complete workshop state.
- CLARITY air-shelf tone control; loudness matched across instruments at
  -18 dBFS RMS; parameter smoothing throughout.
- Six measured test suites (130+ rows) run in CI.

## [0.1.0] — 2026-07-23

### Added

- Physical model of a didgeridoo: lungs, vocal tract, lip valve, waveguide
  bore, bell radiation. No samples and no oscillator.
- Bore: 16-segment Kelly-Lochbaum waveguide over a shapeable radius profile
  (bell, flare, wall texture, wall damping) with a deterministic seeded wall
  irregularity, and power-complementary bell radiation whose corner tracks the
  bell radius.
- Lip valve: one-mass outward-striking exciter after Silva / Menguy-Gilbert,
  integrated with the Newmark scheme (beta 1/4, eta 1/2) plus a fixed point on
  the pressure across the lips. Bernoulli slit flow is solved in closed form
  against tract and bore impedances in series, so the lips beat shut for part
  of every cycle.
- Vocal tract: 8-section waveguide, morphable vowel area functions, and an
  allpass glottis that is open to the lungs at low frequency and reflective at
  formant frequencies.
- Nonlinear wave propagation: amplitude-dependent segment delay, bounded to
  stay stable, so the timbre brightens with blowing pressure.
- Turbulence as a fluctuating pressure jump proportional to the Bernoulli drop
  across the lips (Hirschberg & Verge), gated by the lip opening so it stops
  during the closed phase. Calibrated by measured harmonic-to-noise ratio to
  about +23 dB at the default breath setting.
- Self-calibrating tuning: a linearised lip-plus-bore solver places the bore,
  then a learner measures the sounding period and corrects a length trim per
  frequency band. The correction is taken once per note, after it settles, and
  applied at the next note-on rather than to the note being played, so a note
  holds one pitch throughout. Within a few cents on a first note, inside two
  thereafter.
- Overblowing: a second, much higher held note selects the toot register and
  firms the embouchure automatically.
- MIDI: note on/off, pitch bend, CC2/CC11 breath and expression on the blowing
  pressure. Bend range is settable from zero to two octaves, and the bend
  shortens the tube as well as the lips -- bending the embouchure alone barely
  moves the sounding pitch, since the bore decides it.
- WebView UI with a live cutaway of the instrument driven by the engine's own
  bore profile and vocal tract, plus breath, embouchure, voice, instrument and
  space panels.
- Output spectrum analyser along the bottom of the instrument panel: an FFT at
  roughly 12 Hz per bin over 256 log-spaced display points from 40 Hz to
  16 kHz, with peak hold and the six strongest partials tracked and labelled
  with their measured frequency. It runs only while the editor is open.
- Wave field telemetry: a quadrature detector on the running waveguide reports
  the pressure and the air's displacement at each segment boundary as complex
  amplitudes, so the cutaway reconstructs the real travelling wave instead of
  pulsing an amplitude envelope in step. The air parcels oscillate about fixed
  positions and let their spacing show compression, which is what air does.
- Optional decay stage: with it switched on the breath falls away under a
  held note, turning the model into a struck exciter rather than a drone.
- Velocity routing to breath, breath plus attack, embouchure or brightness --
  the destinations that follow from blowing harder -- with an amount control
  and an explicit off.
- Bore profile: twelve of them, from the natural termite-hollowed tube through
  cylinder, cone and horn to trumpet, trombone, flugelhorn, french horn, tuba,
  alphorn and contrabass. Each is built from the parallel fraction before the
  bell, the bell's flare exponent and the bore width at both ends. This sets
  the resonance series, so it changes the instrument far more than any single
  knob, and profiles carry a level trim so switching does not jump by the
  18 dB they otherwise differ by.
- Brass mouthpiece on the brass profiles: a wide cup narrowing to a tight
  throat, the one non-monotonic part of the bore, modelled with its own two
  short segments. Its Helmholtz resonance is a large part of the brass timbre
  and lifted the trombone's spectral centroid from 123 Hz to 553 Hz.
- Wall material: wood, bamboo, brass, steel or glass, specified by the loss
  it produces over a full round trip and solved back into the per-traversal
  filter. Overblowing now also takes more breath, as it does in life, so the
  higher register survives a damped wooden wall.
- Humanize: an offset drawn once per note plus a slow wander under held notes,
  covering breath, embouchure and tonguing. Measured at the default setting it
  moves successive notes by about three cents and a third of a decibel.
- Excitation type: lips, single reed, double reed or free reed. The striking
  direction is the physical difference -- lips are blown open and sound above a
  bore resonance, a cane reed is blown shut and sounds below it -- and it
  brings a real threshold and a real ceiling with it: a reed begins to speak at
  a third of its beating pressure and is choked above it. Each type carries its
  own measured tuning offset and its own tract coupling, since a reed behind a
  mouthpiece slit is loaded by the player's mouth far more weakly than lips
  are. Every type plays within a few cents across the range on a first note.
- Bore diameter, scaling the whole tube from half to double. Measured across
  the range the spectral centroid runs 490 Hz to 160 Hz: narrow is the bright
  one, because the impedance rise outweighs the extra wall loss.
- Vocal tract coupling deepened from a cap of 2.5 to 3.5 times the bore
  impedance, where the vowel shaping saturates -- a vowel change now carves the
  harmonic series by up to 17 dB. An interim value of 5 was too high and let the
  tract choke the drone into near-silence at full voice with a closed vowel,
  recoverable only by adding growl; that dead zone is gone and a test scans the
  control corner for it.
- Lip damping is held below the point where an outward-striking valve stops
  oscillating. Past it the instrument fell thirty decibels with the pitch
  running away, and the top two thirds of the control ran straight through it.
- Ten factory presets, XML user presets.
- Acoustic test suite that renders audio and measures pitch, spectrum, vowel
  response, growl sidebands, overblowing, silence, numerical stability and
  sample-rate independence.
- Formats: VST3, AU, CLAP, Standalone (macOS/Linux/Windows CI).
