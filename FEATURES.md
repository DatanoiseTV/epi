# Features and honesty ledger

What the models cover, what they do not cover yet, and how each claim is
verified. Every "covered" item below is fenced by at least one measured row
in the test suites (eight suites of numbered rows: offline renders measured
with Goertzel banks, f0 estimators, envelope fits and twin-diff isolation —
never "the code ran"). Every "not covered" item is either a bounded gap with
a named physical mechanism, or a known absence. The gap rows print on every
suite run; nothing on the second list is hidden.

Sources: published measurement papers (DAFx, ISMA, JASA, EURASIP), service
manuals, published sample-set analyses, and practitioner reports. Comments
in the DSP headers cite the source for each constant that has one, and name
the constants that are calibrations.

---

## Cross-cutting: the engine

Covered:

- **Five instruments** — Tine, E-Grand, Reed, Grand, Clav — 88 voices
  each, every note its own physical mechanism (no sample playback, no
  oscillators, no EQ standing in for a transducer anywhere).
- **Continuous sustain pedal (CC64)**: half-pedal everywhere, with the
  felt-compression law (damping raised to the 2.5 power of pedal travel) so
  the audible zone spans the pedal's travel instead of crushing into the
  top. Sostenuto (CC66) latches exactly the held keys. The left pedal
  (CC67) has both real mechanisms as a choice: Shift slides the grand's
  action so the hammer meets two strings off-center, and Rail is the
  upright's half-blow -- the stroke shortens by the square-root law
  (measured -5 dB at full pedal), continuous with the pedal, and the
  lost motion it opens makes quiet strikes land unevenly (the "klapprig"
  action, deterministic per strike and fenced by rows).
- **Sympathetic resonance on every instrument**, each through its real
  path: the Tine's harp, the E-Grand's bridge frame (measured coincidence
  hierarchy: octave -19 dB, twelfth -26, wash -38), the Reed's bar
  proximity, the Grand's soundboard two-port with the full pedal-down
  complement. Feedforward by design — two-way springs pump against string
  Q and were probed and rejected.
- **String materials** (8): music wire, stainless, bronze, brass,
  titanium, aluminium, tungsten, nylon — real constants, geometry
  re-solved at fixed pitch. Non-ferromagnetic conductors are faint through
  magnetic pickups by the eddy-current law, insulators silent; nylon
  sustains because material loss enters only through the bending share of
  the restoring force.
- **Body bench** (8 materials x continuous size): plate-law scaling (mode
  ladder by sqrt(E/rho)/s, modal mass by rho s cubed, added internal loss
  per mode), bit-exact stock at the default. Applies to Tine harp, E-Grand
  frame, Reed frame, Grand board+radiator, and the Clav's case.
- **Hammer coverings** (6): stock, soft felt, hard felt, lacquered,
  leather, wood — each a point in the Hunt-Crossley contact law's
  parameter space relative to the instrument's own stock.
- **Damper felt conditions** (4) with a two-band grip law (a hardened
  damper fails to seat on the fine ripple — the zing), and **keybed
  materials** (4) on the action noise.
- **Transducer swap matrix**: any pickup law on any resonator — magnetic,
  electrostatic, contact — with the physically honest consequences
  (silence and faintness included).
- **Velocity**: launch laws validated against the measured piano
  literature (the shipped default matches the measured curve within
  0.6 dB); a five-point monotone-cubic velocity map editor whose REAL mode
  is a true bypass (identity map short-circuits — raw velocity reaches the
  physics bit-exact, no interpolation in the path).
- **Room**: the adjustable shipped room plus five surveyed profiles
  (booth, studio, stage, hall, church) with Eyring decays computed per
  octave band from published absorption data and image-source early
  reflections; profile switching is click-safe and the shipped room stays
  bit-exact (suite row). Fade-torture survival is a permanent row.
- **Output stage**: five instruments level-matched at a shared -18 dBFS
  mezzo-forte bench; a soft rail transparent below 0.76 and bounded a
  decibel under full scale; instrument switching fades through silence
  with the full chain reset and note events replayed; fast knob sweeps
  glide instead of clicking (click fences in the engine suite).
- **Per-note workshops in the base product**: tine/string length and gauge
  tables (with tuning templates: just intonation, Pythagorean,
  quarter-comma meantone, Werckmeister III, slendro, pelog, octave
  stretch, scatter), per-note pickup height/gap/winding tolerances,
  cabinet dimensions, the grand's course length/gauge lanes.
- **Mic Studio (Grand)**: the calibrated pair (spread, balance, lid
  distance, per-mic trims) and a five-mic positionable stage rendered
  from real geometry — inverse distance (5.99 dB per doubling, measured),
  arrival delay at 343 m/s (cross-correlation lag matches geometry within
  a sample), the board's dipole (a mic under the board reads the low band
  inverted), the lid as a specular image (+3.5 dB of 2-6 kHz on the open
  side), per-tap arrival-based fade-in, all paths unity-gauged at the
  calibrated seat. Classic mode is byte-identical to the shipped chain.
- **Factory presets on all five instruments**, every one of them rendered
  and measured (level, spectrum, and character rows — a zing preset must
  measure its zing); the preset suite also proves the names are unique and
  that the bank's parameter set still matches the layout. Workshop tables,
  mic placements and the velocity map are saved in project state and inside
  every user preset; a state suite proves the round-trips including legacy
  states.
- **CPU (Grand)**: 34% of one core at 48 kHz for a ten-note pedal-down
  chord, 5% for four notes without pedal, 47% absolute worst case
  (88-note fortissimo pedal wash). Deterministic.

Not covered yet (engine level):

- No MPE / per-note expression beyond velocity and channel controls.
- No binaural rendering (the mic stage's geometry could feed one; not
  built).
- No morphing between instruments or between presets.
- Continuous size sweeps on a room profile duck the wet path by design
  (the retarget ramp is the rate limit); the shipped room's size is
  instant. A coarser retarget quantisation would remove the pumping.
- No per-voice output for external mixing (single stereo bus).
- The action-noise layer models key and mechanism noise as one shaped
  event per key; it does not separate key-bottom from hammer return from
  damper lift, which on a real instrument arrive at different moments.

---

## Tine (electromechanical tine piano)

Covered — the mechanism chain: felt hammer (Hunt-Crossley, graded mass
along the compass, let-off with gravity tax) -> cantilever tine with tip
tuning mass, two polarisations, large-deflection coupling -> tone bar
(coupled fork, assembled-mode solve) -> harp/body -> magnetic pickup with
a 2-D field map (the harmonics come from the field geometry, not the
metal — the tine itself is verified near-sinusoidal after the transient,
the central published finding) -> coil (R/L/C with core saturation) ->
preamp and cabinet. The striking line runs the service manual's real rail
(57.15 mm bass to 3.175 mm treble over A0-C8). Pickup position/distance
are the real voicing screws. Stereo vibrato with the measured pan law.

Verified: hammer contact times, tine-motion purity vs pickup THD,
register- and level-dependent growl, bark energy, tuning across the
compass (worst +1.85 cents), attack times, release drop, hard-velocity
tip swings against measured values (3.9/5.5/0.41 mm vs measured
3.1/3.9/0.29).

Not covered yet:

- The three remaining reference gaps (bass fundamental suppression rise
  B6, the second decay slope B5, centroid scaling G2) now have a measured
  cause rather than a suspected one. The field map itself was rebuilt to
  the real two-sheet slug geometry, which changed them barely at all. What
  does close B6 is the transduction law: reciprocity makes the flux a
  product of the magnet's field and the coil's, not linear in the
  magnet's alone. That law also pushes the velocity swing past its
  measured ceiling across the whole physical range of the winding, because
  that row is the spread between two others and the model sits at a corner
  where both endpoints are legal and their spread is not. Adopting it
  means re-deriving the velocity mapping the three rows fence together;
  the numbers for that decision are tabulated in the checklist.
- Bass attack time (C5): measured not contact-limited; the real 14-21 ms
  implies a different tine-side effective mass than the swing rows pin.
  Bounded, mechanism named, not resolved.
- E5's strike transient is about 2 dB cleaner than the reference's
  quietest sample (C1); bounded from below so it cannot silently clean
  further.

## E-Grand (electric grand)

Covered: real strings (one to three per note, constant-beat-rate
unisons), felt hammer with pitch-dependent stiffness, near-rigid bridge
(the long sustain is the absence of a soundboard, not a reverb), plain
wire decay law in the treble, piezo bridge-force pickup with its own
resonance and load high-pass, bridge-frame sympathetic coupling with the
measured coincidence hierarchy, preamp with the shelf EQ's real curves and
rate-limited coefficients, tremolo.

Not covered yet:

- The instrument-specific keybed/action noise layer is shared with the
  piano class rather than measured from this instrument.
- No per-note piezo tolerance table (the electrics' pickup workshops
  cover height/gap/winding for the magnetic instruments).

## Reed (electrostatic reed piano)

Covered: non-uniform cantilever reed with solder tip mass (tuning by
solder is the workshop's real lane), electrostatic pickup as a
time-varying capacitor with the even-far-field capacitance law (the bark
is the 1/(gap-y) asymmetry, verified to vanish when the nonlinearity is
bypassed), supply voltage as a parameter, register voicing on the output
sense, two-stage preamp with true amplitude tremolo, small-box speaker,
bar-proximity sympathetic coupling, mechanical gap/centring glides.

Verified: every row of the Reed suite, including partial tuning through the
full chain (0.040 cents worst persistent deviation — the apparent drift was
harmonic-envelope zero crossings of the composite transducer+clip map,
measured the way the source recordings were measured), even-harmonic
level dependence, attack knock, release characteristics.

Not covered yet:

- Reed replacement variance (a re-soldered reed's mass distribution) is
  not a control; the workshop tunes frequency, not solder geometry.
- Hum/leakage of the high-voltage supply (the real instrument's noise
  floor character) is not modeled.

## Grand (acoustic grand)

Covered: 88 notes of one to three strings on a fitted modal soundboard
(two-port bridge coupling, sense and force through the same shape —
project law), the full course sympathetic with both coupled prefixes and
intra-course detune beats, stretch tuning absorbed the way a tuner
strings it (the coupled pull is tuned out), duplex-free termination
model, hammer with covering bench and una corda geometry, half-pedal felt
law, damper felt conditions, radiator with statistical band above the
board's 1.3 kHz coupling limit, knock through action and shanks, the
mic pair and the five-mic stage, desk EQ, the damper grab (release shh
and pedal-lift wash, twin-diff calibrated at -38 dB) and the pedal thunk
at the stops (the pedal boom emerges from the opened strings rather than
being programmed).

Verified: every row of the Grand suite against a published sample-set
analysis — decay knees and times, unison behavior, Railsback stretch,
attack crests, register
balance, sympathetic selectivity, radiation structure, mic-stage
geometry (six rows), plus the small-sounds fences in the engine suite.

Not covered yet (each bounded, mechanism named):

- A3's decay time reads 1.6 s vs the measured 2.65: its bridge point sits
  in a mobility dip by design (that is what makes its unison rows pass).
  The fix is per-note mobility spread, and it was built and measured
  twice: the volume-velocity form closes this gap and two others, and
  breaks ten rows that were all calibrated against the smooth-mean read
  (decay tables, knees, stereo balance, una corda, the mic seat gauge).
  The mechanism is confirmed; adopting it is a recalibration campaign,
  priced in the research notes rather than half-done.
- The unison-null trio (C4 null depth, A3 no-null, una corda ripple
  ratio) sits on a measured Pareto wall: the shared horizontal-read
  constant moves A3 and C4 in opposite directions, so no shared-constant
  fix exists. The coupled eigen-structure itself is verified correct
  (model's 0.48 Hz reactive pull vs the measured 0.49 Hz beat).
- A0's fundamental balance is 5 dB under the close-mic'd recording: the
  recording's near-field capture (kr < 1) and the fortissimo
  phantom-partial/longitudinal set are both absent. Longitudinal string
  modes are not modeled on any instrument.
- The sympathetic wake level sits above the design-guess band; every
  factor of the model's resonant transfer is pinned by passing rows, so
  this is the model's prediction against a band that was never measured —
  arbitration needs a measurement nobody has published.
- The C3 knee-depth family of targets in the source analysis was proven
  self-inconsistent (beat-null fit artifacts); the re-derived target is
  documented in the research notes.
- No soundboard downbearing/crown state, no seasonal detuning, no
  string-to-string rubbing noise, no key-bottom thump distinct from the
  shared action noise.

## Clav (tangent-action string keyboard)

Covered: struck-and-held strings (the tangent clamps and stays — modal,
with the rubber seat taper and compliance corner), yarn damper release
with the measured three-semitone drop and the aging knob calibrated so
the default is mint condition, twin pickup bars with the measured
positions and 0.5 cm sinc aperture (the every-5th-partial comb emerges
from geometry), four-way pickup switch, the four tone rockers as their
real RC networks behind the measured source impedance, two-transistor
preamp at its measured THD points running at the oversampled rate, the
second-harmonic balance held to the paper's own pickup geometry
(20 log10 of 2 cos (pi d/L) per key).

Verified: every row of the Clav suite, zero gaps — harmonic structure,
comb notches, release drop, decay ripple, drive calibration.

Covered as of the practitioner-report pass (primary evidence recorded in
the research notes, which added its own rows to that suite):

- The case in the pickup: six plate modes derived from the real cabinet's
  dimensions and ply constants (150-602 Hz), fed by the string
  termination forces and the key-bottom thump, sensed as relative motion
  at the pickup rail — the case colors the DI even though it barely
  radiates, reconciling the practitioner's report with the paper's
  "feeble acoustic output". Measured: case energy -30.6 dB under a forte
  fundamental; the key thump's post-release ring is 96.4% concentrated
  at the case mode ladder (the knock-on-wood signature); the body bench
  re-makes the box.
- Per-key mechanical scatter: deterministic +/-30% crimp on the contact
  stiffness and seat exponent — adjacent keys measurably uneven
  (1.6 dB pattern difference) but bounded.
- Tangent rubber wear (WEAR knob): a usage-shaped per-key notch profile;
  worn rubbers catch the string at release (stick then a single let-go
  pulse) and seat roughly at pianissimo. Wear zero is bit-identical to
  the mint instrument.
- Low-velocity chatter, emergent: the stamped-bracket compliance is a
  real series spring, and at pp some keys seat in two or more measured
  contact episodes while forte always seats in one — no injected noise.

Not covered yet:

- The case constants are derived, not fitted: mode table awaits the
  offered isolation recordings (case knocks, muted key presses, string
  pull-offs) from a real instrument.
- Wear is one global amount over a fixed usage profile; a per-key wear
  table (like the tine and pickup tables) is not built.

---

## Constants that rest on inference, not measurement

A model is only as honest as its least-sourced number. These are the
quantities the instruments' physics depends on that NOBODY has published
a measurement of, as far as the project's own source hunts reached. Each
is bounded by physics rather than guessed freely, each is named in the
code where it is used, and each has a cheapest-closure note in the
research registers. They are listed here because "physically modeled"
should not be allowed to imply "every number is measured".

| Instrument | Quantity | What the model does | Closure |
| --- | --- | --- | --- |
| Tine | Pickup coil turns, inductance, capacitance | The coil is a resonant second-order lowpass whose frequency and Q are fitted, not derived from circuit values; DC resistance and wire gauge ARE measured | One LCR reading on a real pickup |
| Tine | Effective pole half-width | Calibrated (1.6 mm) rather than the slug's geometric radius, following the published FEM's finding that only part of the tip carries the field | A field map or Hall probe of a real pickup |
| E-Grand | Strike position (beta) | Assumed 1/8; the service documents genuinely do not contain it, verified by an exhaustive hunt | One photo of any harp from above with a scale in frame |
| E-Grand | Piezo element capacitance | Inferred window (20-50 nF bus, 8-19 Hz corner), twice-derived from physics bounds | One LCR reading on any element |
| E-Grand | Wound-bass per-key lengths and winding schedule | Log-interpolated from the anchored break point | The manufacturer's winding chart |
| E-Grand | Frame material (cast iron vs alloy) | Not modeled as a distinct material; the frame is the shared body bench | Any parts document naming it |
| Clav | Bass string length at F1 | 0.95 m, an invented anchor for the wound tier's log interpolation; the comb rows carry tolerance because of it | A tape measure on a real instrument |
| Clav | Tangent rubber compliance | One calibrated corner frequency; no measurement of the rubber exists | A contact-stiffness measurement, or the offered isolation recordings |
| Clav | Case mode ladder | Derived from cabinet dimensions and plywood constants, not fitted to a real case | The offered isolation recordings (case knocks, muted key presses) |
| Grand | Sympathetic wake level band | The row's band is a design guess, not a measurement; the model's own prediction sits above it | A published pedal-down sympathetic measurement |

## Verification culture (what "covered" means here)

- Eight suites: DSP invariants, Tine reference (vs measurements off the
  real instrument), Grand (vs a published sample-set analysis), Reed,
  Clav, factory presets, engine integration (seams, pedals, clicks,
  rails, rate independence), and state round-trips (the only JUCE-linked
  suite — it instantiates the real plugin processor).
- Failures are failures (CI-gating); known gaps print with their bounds
  and mechanisms on every run and may only shrink.
- Chaotic observables use three-tier bounds so platform floating-point
  differences cannot flake CI.
- A/B and calibration measurements use twin-diff isolation (deterministic
  renders, sample subtraction) so a new mechanism's contribution is
  measured exactly, not estimated.
- Adversarial review is part of the workflow: independent verifier passes
  rebuild the suites from scratch and attempt to refute every claimed
  number. This process has caught real defects before they shipped (a
  +44 cent mistune hiding between test sample points; a room fade
  deadlock; a mic-gauge band tilt).
