# Clavinet D6 implementation plan

How the Hohner Clavinet D6 goes into this engine. Tags: **[M]** measured in
`docs/research/clavinet-measured.md`, **[C]** computed, **[R]** read in a
primary document, **[D]** a design decision with its reason. Companions: that
file, `docs/research/transducers-and-chassis.md` §1.3/§5.3, the EURASIP 2013
and DAFx-12 papers on disk, and the D6/E7 service-manual schematics.

The one-sentence version: a Clavinet note is a **struck-and-held string** — a
rubber tangent pins the string to an anvil for the whole note, so the
resonator carries a full harmonic series and, for once in this plugin, the
resonator makes the timbre and the transducer adds colour: two bar pickups at
different distances from the termination write position combs onto that
series [M], a 4-way switch mixes them in and out of phase [R], four passive
RLC rockers and a two-transistor preamp finish the job [R]. On release the
tangent lets go, the yarn-wrapped dead length rejoins the speaking length,
and the pitch falls **three semitones everywhere on the keyboard** while the
yarn eats the note [M]. That release signature and the every-5th-partial comb
notches are the two sounds that say "Clavinet", and both are geometry, not
effects.

---

## 1. Voice structure

### 1.1 Shape

```
tangent strike -> speaking string (SavModalSystem, ~130 modes, B from Table 1)
                     |            termination rigid while key held
                     +-> tap at bridge-pickup point  (d = 4 cm)      [M]
                     +-> tap at center-pickup point  (d = 18.5->6.5 cm) [M]
                              each: displacement -> flux polynomial -> d/dt
                     -> 4-way switch (center / bridge / sum / difference)
   release: modes retune x 2^(-1/4), yarn damping ramps in          [M]
   60-key bus -> 1:15 transformer sat -> tone rockers (real Z_i(s))
              -> preamp shelves + soft THD -> existing amp/output chain
```

`ClavinetVoice` = one `SavModalSystem<kMaxModes, 2>` + one
`HuntCrossleyHammer` (the tangent tip). No new resonator machinery: this is
`CP70Voice`'s stiff-string modal bank with a different boundary story and a
different readout. The two SAV slots are reserved, not used at launch [D]:
the measured tone is linear in the string (beat nulls and stable f₀ [M]), and
the only candidate nonlinearity — the tangent contact — acts only during the
strike, where the explicit Hunt-Crossley contact already covers it.

### 1.2 Modal, not the paper's waveguide [D]

EURASIP built a DWG loop because they needed 10 voices on a BeagleBoard. We
have `SavModalSystem` already carrying 88 CP-70 strings of the same order,
and modal buys three things the DWG had to approximate or drop:

- **Dispersion in the comb, free.** Their pickup comb is a pure delay; the
  dispersion-corrected comb would have cost +25 % and they shipped without it
  [R, EURASIP §3.3]. A modal readout evaluates the mode shape at the pickup
  point, so notch positions track the stretched partials exactly, at zero
  cost.
- **Per-partial decay set directly.** Their loss + ripple filter pair
  approximates a T60-vs-partial curve; we write each mode's alpha. The
  measured ripple (period 2–3 × f₀, [M]) becomes a deterministic modulation
  of alpha across k, not a randomized feedforward tap.
- **The release retune is a coefficient update**, not a delay-line splice.

Cost is the shipped CP-70 precedent; see §9.

### 1.3 The tangent is a termination with a history [D]

Physically: the key drives the rubber tangent onto the string, presses it
against the anvil, and **holds it there** — the strike and the fret are the
same object. The string splits into the speaking part (anvil → tailpiece,
where the pickups are) and the yarn-wrapped dead part (anvil → tuning pin).

In the model the split is a change of basis, and the tangent's two roles are
separated in time:

- **noteOn**: the voice's modes are the stiff-string partials of the
  *speaking* length — f₀ from the key, B interpolated from the measured
  table [M] — with a rigid termination at the anvil. The strike is the
  Hunt-Crossley tangent tip hitting the string at the anvil end, tip
  velocity mapped from MIDI velocity over the measured **1–4 m/s** range
  [M], contact retained (the hammer never rebounds past the anvil — clamp
  the contact state rather than releasing it). Strike-position comb and
  velocity-dependent brightness ("a heavier touch enhances the proportion of
  overtones" [R]) fall out of the contact, as they do for the CP-70 hammer.
- **while held**: nothing moves. The termination is rigid, f₀ is stable to
  1–2 cents [M], no per-sample tangent work.
- **noteOff**: the tangent leaves, the string is suddenly 2^(3/12) = 1.189×
  longer [C from the measured 3-semitone drop]. Every mode's frequency
  scales by **2^(−1/4)** in place — state kept, coefficients updated — and
  the yarn damping ramps each alpha up over a few tens of ms. The measured
  spectrogram shows the partials *gliding* down through the release [M],
  which is exactly what retune-in-place produces and what a
  project-onto-new-basis scheme would smear; the cheap option is also the
  measured one.

The dead length is never simulated as a second system [D]: while held it is
yarn-damped and does not speak; at release its only audible effect is the
length it adds — which the retune is.

### 1.4 Mode count and tuning

- f₀ per key from equal temperament on the played note. No stretch table:
  B here is 10–100× smaller than the CP-70's [M], the paper found partials
  within their fit tolerance of Eq. 1, and no factory stretch document
  exists. The measured-vs-printed pitch confusion in the paper
  (clavinet-measured.md §11.6) is a reason not to invent one.
- B(key): log-interpolate the six-point measured table (F1 5e−4 → E6 8e−5)
  with the wound/plain step between keys 23/24 at ≈150 Hz [M] — implemented
  as two interpolation segments meeting in a step, same pattern as
  `CP70Inharmonicity`.
- Mode count: everything under the `kModeBudget = 1/π` ceiling. Worst case
  F1 (43.65 Hz, B = 5e−4) needs ~121 modes at 48 kHz [C]; `kMaxModes = 132`
  matches the CP-70 constant and can be shared.

## 2. Losses

Measured targets, not a loss-filter shape [M]:

- Low/mid sustain T60 up to 20 s or more; high notes shorter, as strings do.
- The lowest 2–4 partials ring markedly longest; T60 decreases with k above
  that.
- Superimposed ripple in T60 across k with period 2–3 × f₀.

Alpha law [D]: a smooth base curve alpha(f) fitted to the T60 anchors (same
two-regime approach as `cp70AlphaFast`, refit to Clavinet numbers once a
reference recording is chosen — see open question 6), times a deterministic
ripple term `1 + a·cos(2π f_k / (Rrate·f₀))` with Rrate drawn per note from
the measured 2–3 range and `a` sized so per-partial T60 spread matches Fig. 8
scale. The paper randomizes its equivalent per keystroke [R]; we keep that,
seeded from the voice seed so renders are reproducible.

Beating [M]: only below E4, 0.5–2 Hz, up to 15 dB p-p, intermittent,
mechanism unknown even to the source. Ship the Wurlitzer's solution [D]: a
beat-partner component at f₀ + δ, δ ∈ [0.5, 2] Hz, engaged with modest depth
for keys ≤ E4 only, seeded per note. Phenomenological and flagged as such —
same status as the Wurli's 2.4 Hz partner, with the same measurement named to
retire it (sideband spacing vs harmonic index).

## 3. The pickups: two taps, one polynomial, one switch

### 3.1 Position comb from the readout, not a filter [D]

Each pickup reads the string at distance d from the tailpiece termination, so
mode k's readout weight is `sin(k·π·d/L)` — zero whenever k·d/L is an
integer. That IS the comb: for the analyzed A2-string geometry d/L ≈ 0.21
[C from the measured 544 Hz first notch], giving the notch at every 5th
partial the paper photographed [M]. No comb filter, no delay line, and the
dispersion interaction the paper dropped is exact.

Per-key d: bridge pickup constant 4 cm; center pickup 18.5 cm (lowest) →
6.5 cm (highest), linear across the compass [M]. Per-key L is the missing
measurement (one anchor: 67.8 cm at 161 Hz [M]); until a scale is measured,
L(key) is reconstructed from the wave-speed family c = 2Lf₀ anchored at that
point with c held piecewise-constant per gauge tier [D] — crude, flagged as
open question 1, and the reason d/L (hence notch positions) carries a
tolerance rather than a certainty everywhere except the measured key.

### 3.2 Flux nonlinearity and derivative [M]

Per tap, in the measured signal order (DAFx-12 Fig. 2): displacement y(t) at
the tap → flux via the published 4th-order polynomial (Table 2; gap control
shifts the operating point along the curve, exactly the "string-magnet
distance" knob the authors demonstrated) → time derivative. Only the
vertical axis exists: along-bar sensitivity is measured 25–30 dB down and
the source models it as zero [M]. The polynomial is evaluated per tap per
sample (Horner, 4 MACs) — cheaper than a table and matches the source.

Pickup electrical loading: measured flat within 1 dB [M] — **no coil
resonance stage for the native path** [D]. The existing `coilFreq`/`coilQ`
controls keep meaning only for the Magnetic transducer-swap lane.

### 3.3 The 4-way switch [R]

`PickupSelect { neck, bridge, bothIn, bothOut }` already exists in
`EpiEngine.h` for exactly this. neck = center pickup alone (warm), bridge
alone (bright), sum in phase (full), difference (thin — the fundamental
partially cancels because both taps sit near the same termination [C]). The
switch is a mix decision on the two taps, applied before the flux
polynomial? No — **after** [D]: the real instrument sums two coil voltages,
i.e. two post-nonlinearity signals; summing displacements first would let
one pickup's flux curve see the other's position. Two taps, two polynomial
evaluations, two derivatives, then the switch matrix. No renormalization:
anti-phase is quieter on the real instrument and stays quieter here.

### 3.4 Transducer swap

The founding trick of this plugin: the native transducer is one of four. For
the Clavinet the native path is §3.2; the Magnetic/Electro/Contact lanes
reuse the shared machinery on the same taps (`transducers-and-chassis.md`
§1.5 already specs the bar/blade `MagneticPickup` family for the reverse
swap — Clavinet pickup under a Rhodes tine).

## 4. Tone rockers, preamp, noise

### 4.1 The four rockers are the real networks [R]

From the D6/E7 schematics (identical values, cross-confirmed by EURASIP
Table 3), each rocker switches a passive branch between the two BC 550C
stages:

| Rocker | Z_i(s) | Values |
| --- | --- | --- |
| Soft | R/(1+sRC) | 30 kΩ, 0.1 µF |
| Medium | R/(1+sRC) | 10 kΩ, 15 nF |
| Treble | sL/(1+s²LC) | 2 H, 4.7 nF |
| Brilliant | sL | 0.6 H |

Digitize each Z_i by bilinear transform and cascade the engaged ones —
EURASIP did exactly this and validated the cascade against SPICE (their
Fig. 14) [R], so the shortcut is pre-validated; the full loaded-divider
circuit stays an open question (5) in case the match audibly drifts with
multiple rockers down. Hohner's own rule ships: **at least one rocker down
or the instrument is silent** [R] — enforced by falling back to Medium when
the player clears all four [D], because a DAW parameter that silences the
plugin is a support ticket, not authenticity.

### 4.2 Preamp [M/R]

- Amplifier-minus-tone-stack response: low shelf −3 dB at 130 Hz, high shelf
  +3 dB at 4 kHz [M, SPICE on the real schematic] — two first-order shelves.
- Saturation: soft waveshaper calibrated so THD reads 1 % at the nominal
  full pickup level (the measured 400 mV point) and ≈3.6 % at fortissimo
  chord peaks [M]. `preampDrive` scales into and past that calibration.
- The 1:15 input transformer and the 8.2 V zener rail are why the real
  preamp runs out of headroom from the top, not the bottom [R]; the
  waveshaper is asymmetric-soft accordingly [D].
- EMI/hum: documented character ("a fair amount of noise" [M]) — shipped
  behind the existing noise/`strikeNoise`-adjacent gain at a level default
  near zero [D]; authentic is not the same as wanted.

### 4.3 Knock and mute

- Tangent knock: synthesized through `ActionNoise` (no sampled E6 hit),
  band-limited below the measured ≈1.2 kHz [M], level constant per the
  source's finding that one knock serves all keys [R], so it reads loudest
  against high notes exactly as measured.
- Mute-bar slider: a global damping add-on to every voice's alphas [R
  behavior, no measurement] — parameter deferred (open question 7).

## 5. Dampers and release, player-facing

`damperGrip` maps to yarn efficacy: at 1.0 the measured mint-condition
behavior (fast release, the 3-semitone drop audible only as a blip); lower
values lengthen the release and expose the drop — which is precisely what an
aged, compressed yarn does on real units [R, community lore + VV's gel
replacement business]. The drop itself is not a parameter: it is geometry
and stays fixed at 3.000 semitones [M].

Sustain pedal: the real instrument has none. Pedal holds the tangent state
[D] — keys stay clamped, the retune/damp happens when the pedal lifts.

## 6. Engine integration

- `Instrument::clavinet` already exists in the enum; the *shipping* dispatch
  order is 0 = Tine, 1 = E-Grand, 2 = Reed, so the Clavinet lands as
  **index 3, "Clav"**, appended to `instrumentNames` — stored sessions keep
  their sound, same rule the Wurlitzer plan wrote down. The stale
  enum-vs-names comment gets reconciled in the same change.
- Compass: F1–E6 (MIDI 29–88) inside the engine's 21–108 range. Outside it
  the voice extrapolates the scale design (B, gauge tier, pickup distances
  clamped at their end values) [D] — flagged as invention beyond the real
  instrument, consistent with how the other voices treat their compasses.
- `pickupSel` (the parameter) is today the transducer swap despite its
  comment; `PickupSelect` needs its own parameter ("clavSwitch", 4-way,
  default in-phase sum? — no: **default center-only**, the switch-down rest
  position on the real panel [R]). Four tone-rocker booleans join it. New
  parameters go at the end of the layout, per the repo's established
  versioning rule.
- Base-rate processing, no oversampling [D]: the voice is linear, the flux
  polynomial is 4th order on a millimetre-scale argument, and the preamp
  THD ceiling is 3.6 % — the alias budget test (V12) is the gate, exactly
  as P4 gated the CP-70 decision.

## 7. What ClavinetVoice does per sample

While sounding: advance the modal bank (as CP-70); form two taps as weighted
modal sums (weights precomputed at setNote); two Horner polynomials + two
first-difference derivatives; switch matrix to one output. At control rate:
beat-partner and ripple bookkeeping. At noteOff: one pass over modes scaling
omega by 2^(−1/4) and alphas onto the yarn curve. Nothing else moves during
sustain — the Clavinet is the cheapest voice per sounding second in the
plugin despite the mode count.

## 8. Verification plan

`tests/test_epi_clavinet.cpp`, same row/verdict machinery as the existing
reference tests. Reference chain for spectral rows: one rocker (Brilliant)
down, switch = center, preamp at nominal, room/tremolo/phaser off. Targets
from clavinet-measured.md:

| # | Property | Target | Method |
| --- | --- | --- | --- |
| V1 | Readout comb zeros | weight(k) = 0 at k·d/L integer, exact | unit test on tap weights, no audio |
| V2 | Comb notches, rendered | A2-geometry note: local spectral minima at every 5th partial | spectrum at 0.5 s, minima vs neighboring partials, depth informational |
| V3 | Release pitch drop | 3.00 semitones ± 0.1, at ≥5 keys across the compass | short-window f₀ track through noteOff |
| V4 | B curve | measured table at F1, A1, D3, F5, E6 ± 15 % | peak-fit partials 2–7, LSQ on f_k/kf₀ vs k², the paper's own N = 6 recipe |
| V5 | Wound/plain step | B discontinuity between keys 23/24 | B(23) / B(24) > 1, informational magnitude |
| V6 | Sustain | T60 ≥ 15 s at A2-region keys; monotone ↓ with key above mid | envelope fit |
| V7 | Per-partial T60 ripple | period within 2–3 × f₀ | T60(k) sequence, autocorrelation peak |
| V8 | Second partial | H2 ≥ H1 + 3 dB across mid compass | spectrum at 0.5 s |
| V9 | Switch matrix | anti-phase H1 below in-phase H1 by the sin-weight prediction ± 2 dB; bridge brighter than center (spectral centroid) | render 4 switch states, same note/seed |
| V10 | Tone rockers | each rendered response vs analytic bilinear Z_i ± 0.5 dB, 30 Hz–15 kHz | filter response direct |
| V11 | Preamp THD | 1 % ± 0.5 at nominal drive; 3.6 % region reachable | 1 kHz sine through preamp path, the source's own method |
| V12 | Alias residue at base rate | < −70 dB | Goertzel at folded bins, hard high note, gate for §6's no-oversampling call |
| V13 | f₀ stability in sustain | ≤ 2 cents drift, 0.2–5 s | windowed autocorrelation |
| V14 | Velocity map | tip velocity 1–4 m/s over MIDI 1–127; H-content rises with velocity | telemetry + spectral centroid vs velocity |
| V15 | Knock band | knock energy concentrated < 1.2 kHz | spectrum of noteOn with string muted |
| V16 | Silence rule | all rockers up → Medium fallback, output present | render |
| V17 | No NaN / energy growth | energy never rises in any linear segment | existing S-row machinery |
| V18 | Adversarial CPU | < 0.5 % missed deadlines, 60-key clamp-and-release stress | existing stress harness |

V2's depth and V9's exact numbers are expected to need one calibration pass
against a real D6 recording (open question 6) — the rows say so now.

## 9. Mode budget and CPU [C]

Worst case ~121 modes at F1, comparable to the CP-70's 129 at E1, on the
same `SavModalSystem`; 60 keys vs the CP-70's 88 voices, no oversampling, no
per-sample nonlinearity in the resonator, and two 4th-order polynomials per
voice at readout. Strictly cheaper than the shipped CP-70 path, which is the
budget precedent.

## 10. Deliberately not in this plan

- **The paper's ripple filter, loss filter, beating equalizers, dispersion
  filter, excitation-pulse polynomial** — all DWG-shaped workarounds for
  things the modal engine states directly (alphas, mode frequencies, the
  Hunt-Crossley strike). The excitation polynomial remains in the research
  file as a validation reference for the rendered attack shape.
- **FDTD tangent–string interaction** (their named future work): the
  clamp-and-retune model covers the measured behavior; revisit only if the
  attack fails against recordings.
- **Sampled knock** — synthesized instead (§4.3).
- **Humbucker aftermarket pickups, wah/effects lore** from the FAQ.
- **Direct finger–string coupling** (the manuals' portato note): real, but
  no measurement of its magnitude exists; noted for later.

## 11. Open questions, ranked by risk

1. **Per-key string scale** (lengths/tensions): one length anchor and the
   pickup-distance sweep are all that exist. Wrong L(key) shifts every comb
   notch. Need: tape-measure data or photos of a real harp; until then §3.1's
   reconstruction plus a per-key d/L trim table calibrated on recordings.
2. **Tangent rubber contact law**: no stiffness/damping measurement; the
   attack spectrum rides on it. The V8/V14 rows plus the excitation-pulse
   polynomial are the calibration instruments.
3. **Yarn damping rate**: "short" is the only figure. Measure release T60
   from any D6 recording; drives §5's damperGrip calibration.
4. **Release retune vs projection**: retune-in-place matches the glide seen
   in the spectrogram; if A/B against recordings shows the release too
   clean, the dead-length modes need real treatment.
5. **Tone stack as loaded divider vs cascaded H_i**: EURASIP's cascade
   matched SPICE for the combinations they showed; verify the remaining 11
   rocker combinations against a simulation before trusting all of them.
6. **Reference recording set**: the paper's database is not published. Find
   or make a clean D6 recording set (per-key, per-switch) — every
   calibration row above wants it.
7. **Mute-bar parameter**: worth a knob, needs a measurement first.
