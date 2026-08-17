<div align="center">

# Didge

### Physically modeled didgeridoo — VST3 · AU · CLAP · Standalone

A didgeridoo built from acoustics rather than samples: lungs drive a vocal
tract, the tract drives a one-mass lip valve, and the lips drive a waveguide
bore that radiates from its bell. Play it from a MIDI keyboard and it tunes
itself to the note you asked for.

![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%C2%B7%20AU%20%C2%B7%20CLAP%20%C2%B7%20Standalone-e0913a)
![JUCE](https://img.shields.io/badge/JUCE-8.0-cccccc)
![C++](https://img.shields.io/badge/C%2B%2B-20-555)

</div>

## What it is

There is no sample content and no oscillator. Every sound the plugin makes is
the result of simulating air:

- **Bore** — a 16-segment waveguide with Kelly-Lochbaum scattering junctions,
  built from a radius profile you can shape (bell, flare, wall texture, wall
  damping). The open end uses the standard power-complementary radiation pair,
  so the radiated sound is `incident + reflected` and the bell's high-pass
  corner follows its own radius.
- **Lip valve** — a one-mass outward-striking model after the brass exciter of
  Silva, Menguy-Gilbert et al., integrated with the unconditionally stable
  Newmark scheme and a fixed point on the pressure across the lips. Bernoulli
  slit flow is solved in closed form against the tract and bore impedances in
  series. The lips genuinely beat shut for part of every cycle — that closure
  is where the buzz comes from.
- **Vocal tract** — an 8-section waveguide with morphable vowel area functions
  and a frequency-dependent glottis. The glottis is an allpass: open to the
  lungs at low frequency so the breath passes, reflective at formant
  frequencies so the tract resonates. Tarnopolsky et al. identified exactly
  that "partially closed glottis" as the difference between an experienced
  didgeridoo player and a novice.
- **Nonlinear propagation** — sound travels at `c + beta*v`, so loud waves
  steepen as they go. This is what makes brass instruments turn brassy when
  pushed, and it makes this one respond to how hard you blow.
- **Turbulence** — a fluctuating pressure jump proportional to the Bernoulli
  drop across the lips, after Hirschberg and Verge. Because it scales with the
  jet, it falls silent while the lips are shut, so the breath rides on the tone
  in step with it instead of sitting underneath it as a constant draught.

## Playing it

| Gesture | Result |
| --- | --- |
| Hold a note | The bore retunes and drones on it |
| Hold a second, much higher note | Overblows into the toot register; the embouchure firms automatically |
| Pitch bend | Bends the lips and the tube together, over a settable range up to two octaves |
| CC2 / CC11 | Breath and expression scale the blowing pressure |
| Vowel / Mouth Open | Moves the tongue, colouring the drone |
| Growl | Voiced modulation, as if humming while blowing |
| Exciter | Lips, single reed, double reed or free reed |
| Velocity | Routable to breath, attack, embouchure or brightness |
| Humanize | Per-note and continuous inconsistency, so repeats are never identical |

**Exciter** decides what turns the breath into an oscillation, and it changes
the instrument rather than its colour. There are only a few ways to build that
device, and the one that matters most is which way the pressure across the
valve pushes it:

| Exciter | Direction | Consequence |
| --- | --- | --- |
| Lips | Blown open | Sounds above the bore resonance. The lip resonance sits near the note, so the player picks the register -- which is why a brass player gets a whole harmonic series from one tube |
| Single reed | Blown shut | Sounds below the bore resonance. A cane reed resonates near 2.2 kHz whatever it plays, so the bore alone decides the pitch |
| Double reed | Blown shut | The same, but stiffer, narrower and damped hard by the lips; the hardest here to blow, as it is in life |
| Free reed | Blown shut | A metal tongue that nothing damps but the air, sharp enough that it sets the pitch and the pipe follows it |

An inward-striking valve has a real threshold and a real ceiling. Blowing
begins at a third of the beating pressure -- the classical result for a reed on
a lossless resonator, which this model reproduces exactly because it falls out
of the same equations -- and above the beating pressure the reed is held shut
and the instrument stops. Both ends are reachable from the Breath and Aperture
controls, so a tight reed starts on very little air and chokes early, and an
open one needs more of both. Lips do neither; they only get louder.

The exciters also differ in how much the player's mouth can load them.
Tarnopolsky et al. measured a didgeridoo player's vocal tract dominating the
bore by more than an order of magnitude, through the wide, low-impedance
aperture that lips present. A cane reed sits behind a slit a fraction of a
millimetre high; Chen, Smith and Wolfe found clarinettists need a tract
impedance exceeding the bore's to bend a note or reach the altissimo, and that
only advanced players manage it. Leaving the coupling at the didgeridoo value
lets the tract seize a reed's pitch outright -- measured here, a single reed
above D3 stopped tracking the keyboard and sat on the same three tract
resonances whatever note was asked for.

**Bore diameter** scales the whole tube, half to double, and the two effects it
has pull against each other. The characteristic impedance goes as 1/r^2, so a
narrow tube stands a much larger pressure against the exciter and drives the
wave further into the nonlinear regime; the wall boundary layer is a fixed
thickness whatever the bore, so loss per unit length goes as 1/r and works the
other way. Measured, the impedance wins by a long way: the spectral centroid
runs from about 490 Hz at the narrow end to 160 Hz at the wide one. Narrow is
the bright, brilliant one and wide the broad, dark one, which is how narrow-
and large-bore brass instruments are described.

**Bore profile** is the control with the largest effect, because it sets the
resonance series rather than the tone colour. Twelve profiles are built from
the two numbers that actually separate wind instruments — how much of the
length runs parallel before the bell, and how fast the bell then opens —
together with the bore width at each end:

| Profile | Parallel run | Character |
| --- | --- | --- |
| Natural | none | Irregular termite-hollowed tube |
| Cylinder | all | Odd harmonics only, hollow and clarinet-like |
| Cone | none | Complete harmonic series, reedy and saxophone-like |
| Flared / Horn | none / late | Smoothly opening horn |
| Trumpet | a third | Narrow and bright; harmonics rise above the fundamental |
| Trombone | half | The most cylindrical of the brass |
| Flugelhorn | little | Conical and mellow |
| French Horn | little | Narrow throat, wide late bell |
| Tuba / Contrabass | little | Very wide bore and bell, large and dark |
| Alphorn | almost none | Long gentle cone |

The brass profiles also carry a **mouthpiece**: a wide cup narrowing to a very
tight throat before the bore proper. That is the one place the bore is not
monotonic, and the constriction working against the cup volume is a Helmholtz
resonator whose resonance is a large part of why brass sounds like brass.
Adding it moved the trombone's spectral centroid from 123 Hz to 553 Hz and the
flugelhorn's from 158 Hz to 457 Hz, with harmonics standing above the
fundamental rather than falling away from it.

Measured on a held D2, the trumpet's spectral centroid is around 670 Hz against
the natural bore's 195 Hz, and the cylinder's second harmonic sits 12 dB under
its fundamental where the natural bore's sits 7 dB above. Profiles carry a
level trim so switching between them does not jump by the 18 dB they otherwise
differ by. **Material** sets how much the wall loses and how
sharply that loss rises with frequency: wood is dark and short, metal brighter
and longer-ringing. **Decay**, off by default, lets the breath run out under a
held note for short struck sounds.

The instrument tunes itself. The linearised solver places the bore so the
threshold oscillation lands on the requested note; a learner then measures the
sounding period from the lip oscillator and corrects a length trim, cached per
frequency band. This is necessary because a didgeridoo is driven far past
threshold, and how sharp it plays depends almost entirely on bore shape and
blowing pressure — from about 10 to 180 cents on this model, which no fixed
correction curve covers.

The correction is taken **once per note, after the note has settled, and
applied at the next note-on** — never to the note being played. Steering a
sounding note is heard as portamento. A note therefore holds one pitch for its
whole length; on the default bore the first note of a session lands within a
few cents and later ones inside two.

The cutaway shows the wave itself, not a picture of one. The engine runs a
quadrature detector on the real waveguide and reports the acoustic pressure and
the air's own displacement at every segment boundary as complex amplitudes --
magnitude and phase. Phase is the part that matters: it is what distinguishes a
wave travelling toward the bell from a pattern standing still, and an amplitude
envelope cannot carry it. The interface reconstructs the field at its own frame
rate, so the filled column is the amplitude, which fixes the nodes, and the line
inside it is the pressure at that instant, which moves. Where the bore reflects
strongly the phases line up and the line stands; where the bell radiates the
phase advances and the crest runs toward it. Neither behaviour is drawn in --
there is no travelling-versus-standing switch, only the measured field.

The parcels of air are the same field, and they behave the way air does: each
one oscillates about a fixed place and does not travel with the wave. What
travels through them is the disturbance. Because neighbouring parcels are
displaced by slightly different amounts they crowd and spread, and those
compressions and rarefactions sit a quarter cycle from the swing and move along
the tube on their own -- nothing in the drawing puts them there. On top of that,
and much smaller, is the steady drift of the breath actually leaving the bell:
real, but the small term, which is the reverse of how it is usually drawn.

The strip along the bottom of the instrument panel is a live analyser: an FFT
sized for roughly 12 Hz per bin, drawn over 256 log-spaced display points from
40 Hz to 16 kHz, with a slow-falling peak hold and the six strongest partials
tracked and labelled with their measured frequency. Partial frequencies are
recovered by fitting a parabola across each peak, so they are reported far
finer than the bin spacing. It runs only while the editor is open.

This began as a constant-Q filter bank, which was the wrong instrument for the
job: at a quarter-octave the bands are wider than the 73 Hz spacing of this
drone's harmonics above about the third, so everything merged into an envelope.
The FFT resolves them and, per sample, costs less than thirty-two filters did.

## Installing

Tagged releases carry a universal macOS build and a Windows x64 build of every
format, under [Releases](https://github.com/DatanoiseTV/didge/releases). Both
are unsigned, so the first launch needs one confirmation step; the archive
contains an INSTALL note with the exact commands. Building from source avoids
that entirely.

## Building

```sh
git clone --recursive https://github.com/DatanoiseTV/didge
cd didge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Didge_All Didge_CLAP
```

Built plugins are copied into the user plug-in folders by default
(`-DDIDGE_INSTALL_LOCAL=OFF` to disable, e.g. in CI).

Run the tests:

```sh
cmake --build build --target didge_dsp_tests didge_state_tests
ctest --test-dir build --output-on-failure
```

## Tests

The DSP tests render audio and measure it rather than asserting that the code
ran. They check that every note from 43 to 147 Hz sounds within 12 cents of the
pitch requested, that the spectrum is buzzy rather than sinusoidal, that the
vowel control measurably rewrites the spectral envelope, that growl adds
inharmonic sidebands, that overblowing reaches a higher register, that the
instrument falls genuinely silent with no breath, that output stays finite and
bounded across parameter extremes, and that pitch does not depend on sample
rate.

## Known limitations

- **Lip Q cannot reach the measured human range, and that ceiling is the same
  one that limits the wall material.** Real human lips measure Q = 0.46 to 1.8;
  the Q around 7 common in the literature is an artificial-lip value. Measured
  on this model, an outward-striking valve stops oscillating at a damping ratio
  of about 0.18, which is Q = 2.8, and no amount of breath recovers it: the
  drive available to the valve falls as the square of the damping while blowing
  pressure only helps as its square root. Sweeping the engine's whole pressure
  range at Q = 1.7 leaves the drone thirty decibels down. The reason is bore
  loss -- the loop gain has to come from somewhere, and a lossier tube gives
  the valve less to work with. That is also why wall material is subtler here
  than the name suggests: loss strong enough to make wood and metal obviously
  different is loss the overblown register cannot survive. The two are one
  problem, and the fix for both is a lower-loss bore, which means modelling the
  wall's square-root-of-frequency boundary-layer loss properly instead of
  fitting a one-pole to it at a reference frequency. Until then the damping
  control is held below the edge rather than being allowed to switch the
  instrument off.
- **Vocal shaping is deeper than it was, and its real ceiling is now known.**
  The old note said the model became unstable above a tract-to-bore impedance
  ratio of 2.5 and was capped there. That is not the limit; the model stays
  finite far higher. But there is a ceiling: measured across the whole vowel and
  pressure grid, the shaping depth saturates at a ratio of 3.5 -- about 17 dB of
  swing between oo and ee -- and nothing above it deepens the vowel further,
  while past about 4 the tract begins choking the lips into silence at its
  extreme. The cap sits at 3.5, the full shaping the model can give with margin
  below the point where the drone dies. Measured tract peaks in real players are
  around 18 times the bore, so there is still headroom in principle, but reaching
  it needs the tract's loading moved into its returning wave rather than applied
  instantaneously -- the same change that would let lip Q rise.

- **Cane reeds let the tract take the pitch at extreme settings.** With the
  voice control at maximum and the vowel at either extreme, a single reed stops
  tracking the keyboard and sits on a tract resonance. That is a real mechanism
  -- it is how clarinet pitch bending and the altissimo work -- but it arrives
  here sooner than it should, so reed tract coupling is set low enough to keep
  it out of reach of the controls.

## References

- Tarnopolsky, Fletcher, Hollenberg, Lange, Smith & Wolfe, "The vocal tract and
  the sound of a didgeridoo", *Nature* **436**, 39 (2005).
- Fletcher & Rossing, *The Physics of Musical Instruments*, ch. 13-15, for the
  classification of pressure-controlled valves and the reed and lip parameters.
- Dalmont, Gilbert & Ollivier, "Nonlinear characteristics of single-reed
  instruments", *JASA* **118**, 3294 (2005), for beating and threshold pressures.
- Facchinetti, Boutillon & Constantinescu, "Numerical and experimental study of
  the vibrations of a clarinet reed", *JASA* **114**, 3345 (2003).
- Chen, Smith & Wolfe, "Pitch bending and glissandi on the clarinet: roles of
  the vocal tract and partial tone hole closure", *JASA* **126**, 1511 (2009).
- St. Hilaire, Wilson & Beavers, "Aerodynamic excitation of the harmonium
  reed", *JFM* **91**, 693 (1979).
- Smith, Rey, Dickens, Fletcher, Hollenberg & Wolfe, "Vocal tract resonances
  and the sound of the Australian didjeridu", *JASA* **121**(1), 547 (2007).
- Silva, Vergez, Guillemain, Kergomard et al., "Time-domain simulation of brass
  instruments", arXiv:1511.04247.
- Hirschberg & Verge, "Turbulence noise in flue instruments", ISMA 1995.
- Silva, Guillemain, Kergomard, Mallaroni & Norris, "Approximation formulae for
  the acoustic radiation impedance of a cylindrical pipe", *JSV* **322** (2009).

## License

GPL-3.0. See [LICENSE](LICENSE).
