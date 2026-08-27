# Changelog

All notable changes to Epi are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semver](https://semver.org/) (pre-1.0: minor bumps may break).

## [Unreleased]

### Fixed
- The hammer's hysteretic loss term is solved implicitly instead of
  explicitly. It is a velocity-proportional force, and integrating one
  explicitly is stable only while `c dt / m` stays under 2; measured at ff
  across the grand's compass it ran 1.5 to 30 times past that. The contact
  did not chatter quietly: `(1 + lambda ddot(delta))` went negative on every
  note, the guard half-rectified the force, the string's top modes were
  pumped to tens of metres per second, and the force came out 34 N, 160,
  0, 242 sample to sample. Letting both bodies answer the force inside the
  step gives it in closed form -- one divide, no physics changed, and the
  denominator is 1 wherever the explicit form was already valid.
- The soft pedal made the top of the grand's keyboard LOUDER, by as much as
  9.7 dB. That was the chatter above, seen from the outside: striking two of
  three strings is a 1.5x change in load, worth about six per cent of
  contact time, and it was moving contact 34 per cent and tripling the peak
  force at conserved impulse. The soft pedal now drops the level at every
  note on the compass, which is the only thing a soft pedal can do.
- The grand's hammer stiffness anchors, re-fitted against the same published
  contact times now that the contact they were fitted through has stopped
  chattering: 5e8 / 8e9 / 1.5e11 at C2/C4/C7, an order of magnitude back
  toward the literature, with contact measuring 3.69, 2.06 and 1.17 ms
  against targets of 4, 2 and 1. `lambda` is 0.15 s/m, which is
  Hunt-Crossley's own `3 (1 - e) / (2 v)` for a felt restitution near 0.6 at
  an ff arrival, not the 1.0 that stood there -- a value that needs a
  restitution of -0.2.
- Half-pedal was not monotone on the grand: a tenth of a pedal measured
  16 dB QUIETER than no pedal at all, and a player rolling the pedal on
  heard the note dip and recover. The sympathetic gate opened on the pedal
  being touched at all, while the dampers do not begin to lift until three
  tenths of travel, so the whole harp joined the board against still-seated
  dampers and the struck note paid for the coupling. The gate is now the
  threshold the damper itself calls free.
- Sostenuto caught notes struck after it was already down, if the host
  repeated the controller message -- which hosts do on transport start,
  on punch-in, and from any controller that sends its position
  continuously. Measured 53 dB up on the same note unpedalled, i.e. a note
  hanging until the player releases a pedal they are already holding. The
  catch now happens on the pedal's edge, not on every message that says
  "down".
- One tine could be cut permanently to a voicing the panel does not show.
  The tine bank was rebuilt on note-on whatever instrument was playing --
  alone among the five -- so a voice marked stale by a workshop edit or a
  tuning change was re-cut by a note struck on another instrument, using
  whatever the tine-only controls read at that moment. Putting the control
  back did not undo it: the comparison found no change and that voice was
  never re-cut again.
- A reset did not return the instrument to what it was, which is what a
  bounce is. reset() restored neither the tine bank's record of its last
  configuration nor any of the five banks' version arrays, though prepare()
  sets all of them. Peak residual across six reset cycles goes from
  0.00193 dB to 0.00003.
- The strings, reeds and rods were animated but could not be seen to move on
  four of the five instruments. Swing is real displacement times the rod's
  drawn-to-real ratio, and that ratio is not one number -- a tine is 1.14
  px/mm where a grand string is 0.18 -- so a ff C4 swung 5.3 px on the tine
  and 0.98 px on the grand, under the width of its own line. The magnifier
  is now per instrument. The grand was also the only one publishing
  something that was not a displacement at all.
- The interface jumped when the instrument changed. The card's height
  followed the panel content, 400 design px against 378, and the card is
  centred and scaled to fit -- so those 22 px moved everything on screen by
  35 and resized it on the way.

### Added
- Eighteen rows for the pedals as a player works them, rather than for what
  each pedal is: half-pedal never damping harder on any of the five, the
  sostenuto rule from both sides including the re-sent message, the left
  pedal sparing a note already sounding, the grand's two pedals arriving at
  instruments that do not have them, and a note damped to silence staying
  dead when the pedal comes back down.

### Changed
- Close Pop, Parlor and Harp Grand re-trimmed: the new attack moved them off
  the level bench.
- Harp Grand's note said nylon rings the top clear where wire crowds it. It
  cannot: the bending loss carries the material's loss factor minus the
  stock wire's, so wire's is identically zero and nylon's is 2.0e-2, and the
  top goes an order of magnitude faster. The preset is the short, clean,
  dark one -- harmonic, because nylon's stiffness is nothing against its
  tension, which is what it was always for.

## [0.8.0] - 2026-08-26

### Added
- Per-note tuning over MPE, so a host's tuning system can tune the
  instrument. The offset is a TUNING instruction latched at the strike:
  a wheel moved while a note rings does nothing to it and reaches the
  next note struck on that channel instead, which is the tuner moving to
  the next string. Pinned against the MMA specification, measured out of
  the rendered audio (within 0.02 cents on four instruments, 0.3 on the
  grand's three-string unison), and proved inert when unused --
  byte-identical over 1.44 million samples.
- A repeating octave in the tuner: an octave selector and a REPEAT
  button that stamps twelve offsets across the compass, because most
  tunings are a twelve-note pattern repeated at the octave.
- Voiced Grand, the concert instrument after the technician has been at
  it -- the shipped one carries a factory-fresh hammer, which is bright
  by construction.
- Demos for the whole instrument rather than the tine piano alone, plus
  the A/B pairs that show what this plugin is: one physical thing
  changed, everything else identical.
- A headless smoke test for the interface, since the suites never load
  the page.
- Gigged Clav: the instrument that has been out every weekend for
  thirty years -- notched tangent rubbers that catch on release, a
  rattlier case.
- Test coverage where it was thin: the click fence sweeps all
  thirty-three continuous parameters (was thirteen), a new section
  proves every instrument rings through a single retune -- workshop
  edit, material swap, body resize, tuning knob -- without resetting
  or stepping, the Clav and the two stage-carrying grand presets got
  their first character rows, and the state suite now round-trips all
  forty-nine parameters and checks that every bound parameter is
  declared in the UI bundle.
- Two grand presets that ship a mic-stage placement -- Jazz Club (a
  close pair over the open lid, stage room) and Cathedral (a far high
  pair with a center fill, church room) -- riding a new factory-table
  mechanism for stage placements; every other preset keeps leaving the
  player's stage alone, proved by a state-suite row.
- The Clav's case and its mechanical truth, from a practitioner's
  report (recorded as primary evidence): six plate modes derived from
  the real cabinet reach the pickup by structure-borne sound (a pickup
  senses relative motion, so the box colors the DI without radiating),
  the key-bottom thump rings the case as the knock-on-wood sound,
  crimped rubbers scatter the contact per key, a WEAR knob notches the
  tangent rubbers (catches and clicks, unevenly per key, mint at zero
  and bit-identical to before), and pianissimo seating chatter emerges
  from the stamped-bracket compliance -- measured contact episodes, no
  injected noise. Eighteen new rows.
- The left pedal's second mechanism: SOFT PEDAL chooses Shift (the
  existing una corda) or Rail (the upright's half-blow) on the grand.
  Rail reads CC67 continuously: the stroke shortens by the square-root
  law (-5 dB at full pedal, measured) without re-voicing the unisons,
  and the lost motion it opens scatters quiet strikes (the klapprig
  action) -- both fenced by rows.
- The grand's small sounds: the damper grab (felt re-seating on a
  ringing string chatters against it -- the release shh, and the
  pedal-lift wash over a ringing bank, calibrated by twin-diff at
  -38 dB against the note's own attack) and the pedal thunk (the
  trapwork thumps the board at its stops through the plate's own
  modes; the press rings louder because it excites the freshly opened
  strings -- the pedal boom emerges rather than being programmed).
  Dead strings grab exactly silently and half-pedal work between the
  stops is silent by hysteresis; four engine rows fence it all.
- Room profiles: the output-stage room grows five surveyed spaces next
  to the shipped size-mapped one -- booth, studio, stage, hall, church
  -- each with an Eyring decay computed from published absorption data
  per octave band, image-source early reflections, and the size knob
  scaling the room's linear dimensions about the survey. Profile
  switching rides a 15 ms fade to a null so nothing clicks mid-note;
  the shipped room stays bit-exact (proved by a suite row). Measured:
  the hall holds 15 dB more late-tail energy a second after release
  than the shipped room, the church rings 6.5 s at 500 Hz against the
  computed 6.5.
- Velocity map presets seated on the measured literature: CONCERT
  carries square-law ordinates at the measured 30 dB piano span, STAGE
  gets a floor so a light touch still speaks, and REAL remains a true
  bypass -- at the identity map the raw velocity reaches the physics
  bit-exact, no interpolation in the path.
- The Mic Studio's stage: up to five freely positionable microphones
  dragged on a top-down view of the instrument, each with height, gain
  and pan. Every mic is rendered from real geometry -- inverse
  distance (measured 5.99 dB per doubling), arrival delay at 343 m/s
  (two mics half a metre apart cross-correlate at the geometric lag
  within a sample), the board's dipole (a mic under the board reads
  the low band inverted, correlation -0.81), and the lid as a specular
  image toward the open side (+3.5 dB of 2-6 kHz on the open-lid
  seat). Classic Pair mode is the calibrated pair, byte-identical to
  the shipped chain by a suite row; the whole five-mic stage costs
  2.4% of a core at 48 kHz. Placements persist with the project and
  presets.

- An eighth suite, the state round-trips: the only one that is not
  framework-free -- it instantiates the real plugin processor and
  proves the benches (mic stage, pair trims, velocity map) restore
  exactly through host project state, saved user presets, factory
  loads, and legacy states saved before a bench existed.

### Fixed
- Turning a bench control while notes are ringing no longer clicks. The
  engine rebuilt SOUNDING voices first, on the reasoning that they are
  the ones you can hear -- which is exactly why it was audible: a
  rebuild re-solves the geometry and retunes every mode under a note in
  the air, and sweeping MATERIAL under a held grand chord measured two
  hundred times the signal's own worst step. A change now applies at
  each voice's next strike, because you cannot change the felt on a
  hammer that has already struck or restring a piano while it rings.
  The board follows the same rule. Idle voices rebuild immediately and
  the note-on path rebuilds a stale voice before it sounds, so nothing
  is heard late.
- reset() never cleared the reed bank, so a reed caught mid-ring rang
  through a host's reset into the next render.
- The trapwork thunk's length was written in samples, so its bandwidth
  followed the sample rate and its level moved 14 dB across the
  supported range.
- Renders now repeat across a reset: the free-running generators and the
  block-rate smoothers were left where they stood.


### Changed
- KEY NOISE works on every instrument it is offered on. The panel shows
  the control for all five; it was consumed by three. The grand never
  had an action layer at all -- its key noise now enters through the
  frame, the same path the pedal thunk uses, so it inherits the board's
  colour and the body bench with it, and the keybed selector is no
  longer hidden there. The Clav's key-bottom thump into the case (the
  knock-on-wood the practitioner describes) now answers the control
  instead of being fixed. And the two electrics carried the layer 80 dB
  under the note -- inaudible, which is the same as not having it -- on
  a gain that had never been measured; it now lands at -52 and -48 dB,
  the same subordinate band the tine and grand sit in. Five rows fence
  it, and a stale comment claiming the CP had no frame path for the
  knock is gone.
- Three automation clicks fixed, found by sweeping the whole knob
  surface instead of a third of it: the phaser applied its mix and
  depth at block rate (0.20 worst-sample against a 0.0033 signal
  floor -- an audible tick on every automation chunk) and both
  tremolos stepped their depth the same way. All three now glide with
  a first-call snap.
- The pickup field map carries the magnet's far half: a slug has two
  sheets of magnetic charge, and the map integrated only the near one
  over an infinite strip. It now has both (separated by the patent's
  magnet length), a disc footprint, and a closed form for the third
  dimension -- so the field collapses the way a finite magnet's does
  where a hard bass tine swings, and reverses sign beside the magnet
  where the flux returns. Every calibrated row holds.
- The measured-gap ledger shrank from 17 to 11 with zero failures, by
  a multi-agent deep-dive with adversarial verification: the reed
  chain's tuning row and both clav second-harmonic rows now pass
  against targets derived from their own sources' measured geometry;
  the tine bank was recalibrated on the service manual's real
  striking line (compass tuning and the 300 ms inharmonic residue
  close; three remaining tine gaps traced to one named root cause in
  the pickup field map); the grand's C3 knee target was re-derived
  after being proven arithmetically inconsistent with its own
  measurement set; and the verification pass itself caught and fixed
  a +44 cent mistune at G#1 introduced by the recalibration before it
  ever shipped, plus a room-profile fade deadlock that could silence
  the wet path until reset.
- The grand runs 2.2x lighter: a ten-note chord with the pedal down
  dropped from 75% to 34% of one core at 48 kHz (4 notes without
  pedal: 5%; the true worst case, an 88-note fortissimo pedal wash,
  measures 47%). Sympathetic voices keep the FULL course -- every
  string, both coupled prefixes, so the intra-course detune beats that
  are the shimmer of a pedal wash survive (measured: 8.6 dB of
  coherent wash and the superposed beat structure against a
  single-string economy that was tried and rejected) -- truncated only
  above the board's 1.3 kHz coupling band, which is the physics of the
  two-port, not a budget. The speed came from the bridge exchange:
  raw-array views and hand-split accumulators so the per-voice dots
  pipeline instead of serialising on the FMA latency chain, plus a
  fused coupled-tick fast lane and a dormancy tier with a
  thousandfold hysteresis. Bit-for-bit deterministic; all seven
  suites pass unchanged.

## [0.7.0] - 2026-08-23

The bench release: every part of the instrument a technician can touch
is now a selector with the physics attached, the bank is rebuilt around
them, and a seventh measured suite fences the engine.

### Added
- The grand's string workshop: per-course length and gauge, pitch
  following 1/L and inharmonicity d^2/L^2, with the gauge also carrying
  its tension -- a fatter wire at pitch takes proportionally more,
  which moves the string's impedance into the bridge (measured: a 0.6x
  wire plays 5.8 dB softer and holds its energy, a 1.6x wire 4.6 dB
  louder with the heavier drain).
- The body bench: every instrument's frame, bar or board can be re-made
  (stock, three soundboard woods, four metals) and re-sized (0.7x to
  1.43x), with the plate family's own physics -- mode ladder by
  sqrt(E/rho)/s, modal mass by rho s-cubed, internal loss added per
  mode, bit-exact stock at the default. A steel frame carries 24 dB
  less sympathetic wash than the stock casting; a small light one 24 dB
  more. On the grand the whole board ladder, mobility and radiator tail
  move together.
- Grand string material: the shared MATERIAL selector now reaches the
  grand -- bronze halves the measured inharmonicity, nylon keeps its
  fundamental and sheds highs through the bending share, stock is
  bit-exact by the suite's own rows.
- The hammer bench: soft felt, hard felt, lacquered, leather and wood
  coverings, each a point in the contact law's parameter space relative
  to the instrument's own stock (attack centroids on the tine run
  369 Hz under soft felt to 1155 under wood).
- The grand's desk: a channel-strip shelf pair on the shared bass and
  treble knobs -- outboard, not instrument.
- The factory bank rebuilt around the benches: 59 presets, every one
  rendered and measured, with 24 character rows (a hardened-felt zing
  preset must MEASURE its zing, a worn keybed its thump, a parlor
  board its thinner bottom). Five frozen references untouched.
- The action bench: damper felt condition (fresh, worn, or hardened --
  hardened felt cannot seat on the string's fine ripple and the high
  partials escape it, measured plus 26 dB of post-release zing on the
  string piano) and the rail cloth the keys land on (fresh felt
  quieter and deeper, leather the soft thock of an older action, worn
  cloth louder and brighter).
- A seventh measured suite: 110 engine-integration rows covering all
  five instruments at three sample rates -- life and loudness parity,
  seam behavior, all three pedals per instrument, the full material and
  transducer audibility matrix, knob-sweep click bounds on thirteen
  parameters, rail and 88-key stress, MIDI robustness, and cross-rate
  decay consistency. Zero failures, zero gaps at head.
- README rebuilt for the five-instrument reality, with Materials and
  Pedals sections, the Mic Studio, and fresh screenshots.

### Fixed
- The Clav's tone rockers now divide against the pickup's source
  impedance instead of an ideal source: the Treble branch's LC computed
  to a Q near 340 and screamed at 1.6 kHz under the funk registration
  (26 dB less high-band hash after), and Brilliant becomes the 1.5 kHz
  high-pass a bare inductor to ground physically is.
- The output rail's ceiling moved to -1 dBFS: topping at exactly full
  scale read as clipping on host meters and left nothing for
  inter-sample peaks.
- The loudness bench raised to -18 dBFS mezzo-forte across all five
  instruments -- the plugin sat quiet in sessions.
- The plugin window grew to hold the new benches; the bottom row was
  clipping.
- Six engine defects the new suite caught on its first run: the
  instrument-switch seam froze several chain stages mid-signal and
  snapped them on re-entry (a -12 dBFS burst on a tight switch tour);
  the Clav bank was prepared without the shared field table, so its
  swapped magnetic transducer was silent; the Clav's magnetic paths
  hard-gated non-ferrous strings instead of applying the eddy law; the
  Clav contact transducer sat 33 dB under its native level; the Tine's
  electrostatic swap ignored the insulator rule; and three automation
  click sources -- the cabinet's hard bypass branch at mix zero, the
  E-Grand preamp's block-rate shelf coefficient steps, and the air
  shelf's -- are now continuous under any sweep.
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
