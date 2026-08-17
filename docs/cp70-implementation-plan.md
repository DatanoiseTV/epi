# CP-70 implementation plan

> **Erratum (2026-08-17, after `docs/research/cp70-open-questions.md`):** the
> preamp scoop's Q is **0.27**, not the 0.55 carried below — half as sharp,
> re-fitted by digitising the response plot against its own grid (residual
> 0.39 dB; centre 488 Hz, depth −13.4 dB). Wound-bass tension is **450–600 N**,
> not ~800: the 679 mm figure is the CP-70's E1 (resolved geometrically by the
> two case depths), which brings the bass modal masses down accordingly. The
> full 88-key factory stretch table is now available and replaces the
> interpolation. The damper gate at A6 is confirmed at the parts-list level.
> The pickup bus is six blocks joined directly with two 4.7 k resistors at the
> top, not the mixing-capacitor chain. Use the open-questions doc's numbers
> where they differ from this plan.

How the Yamaha CP-70 goes into this engine. Every number below is tagged the
way `docs/research/cp70-measured.md` tags them — **[M]** measured there, **[C]**
computed, **[R]** read in a primary document — or **[D]** a design decision this
plan makes, with its reason. Companion docs: `docs/research/cp70-measured.md`
(the measurements), `docs/research/yamaha-cp70-cp80.md` (construction).

The one-sentence version: a CP-70 note is one or two stiff strings on a rigid
per-note piezo bridge, each string two polarisations of a plain modal bank with
no nonlinearity anywhere in the voice, read out as termination **force**
(an explicit n-weighting), summed linearly onto one bus, and coloured by a
deeply mid-scooped preamp. The resonator is linear and uncoupled; the pickup
law and the preamp make the timbre. That inverts the Rhodes cost structure:
many more modes, no per-voice transduction path at all.

---

## 1. Voice structure

### 1.1 Shape

```
hammer -> string 1 (V + H polarisation)  --\
       -> string 2 (V + H polarisation)  ---+--> per-note bridge force (n-weighted)
                                             |        (x region trim)
             73+ voices ---------------------+--> one bus -> 12 Hz HP -> preamp
                                                       -> tremolo panner -> out
```

- `CP70Voice` holds **one or two `CP70String`s** and one `HuntCrossleyHammer`.
- Each `CP70String` is one `SavModalSystem<kMaxStringModes, 2>` carrying the
  vertical block then the horizontal block, exactly as `RhodesVoice` lays out
  tine V/H. **No SAV term is active at launch** (section 12, open question 8);
  the two term slots are reserved for a quadratised hammer contact and a
  Kirchhoff stretch term if measurement demands them.
- **The two strings of a bichord are separate systems with no coupling term of
  any kind.** This is the strongest single finding in the research [M]: the C4
  fundamental resolves into two independent pairs with no symmetric/
  antisymmetric splitting, and the D3 beat nulls reach −42 dB — the signature
  of pure superposition, which coupled modes would fill in. Weinreich's
  prompt/aftersound mechanism requires a moving support; the CP terminates on
  a piezo block bolted to a casting, and the rigid-limit prediction (no energy
  exchange) is what the instrument measurably does. Implementing any bridge
  admittance here would be adding a defect.
- The two polarisations of one string are likewise uncoupled (linear voice, no
  cross terms). They share the hammer strike and the readout, nothing else.

### 1.2 Strings per note [R][M]

```
n_strings = 1 for MIDI <= 42 (E1..F#2), 2 for MIDI >= 43 (G2 up)
```

Pinned three ways: the CP-70B's "first 59 strings" of copper winding = 15
single + 22 double wound notes exactly (15 + 44 = 59); Modartt's break at
MIDI 42; and the measurement — F#2 shows one component pair, A#2 and up show
two. (The parts-list phrase "keys 1-22" is CP-80 numbering, key = MIDI − 20;
both models break at the same pitch, G2.)

### 1.3 Mode counts per string [C]

Budget: `SavModalSystem::kModeBudget = fs/pi` (15,279 Hz at 48 kHz) — the
explicit hammer contact imposes it exactly as on the Rhodes. Mode k of a
string sits at `f_k = k f0 sqrt(1 + B k^2)` (section 2), so per note:

| MIDI | note | f0 (Hz) | B | K vertical | K horizontal | strings | modes/note |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 28 | E1 | 41.2 | 1.02e−3 | 105 | 24 | 1 | 129 |
| 35 | B1 | 61.7 | 5.4e−4 | 98 | 19 | 1 | 117 |
| 42 | F#2 | 92.5 | 2.8e−4 | 90 | 13 | 1 | 103 |
| 43 | G2 | 98.0 | 2.6e−4 | 89 | 12 | 2 | 202 |
| 50 | D3 | 146.8 | 2.6e−4 | 69 | 8 | 2 | 154 |
| 57 | A3 | 220.0 | 3.1e−4 | 51 | 5 | 2 | 112 |
| 65 | F4 | 349.2 | 4.9e−4 | 34 | 3 | 2 | 74 |
| 72 | C5 | 523.3 | 9.4e−4 | 23 | 2 | 2 | 50 |
| 80 | G#5 | 830.6 | 2.0e−3 | 15 | 1 | 2 | 32 |
| 88 | E6 | 1318.5 | 4.1e−3 | 9 | 1 | 2 | 20 |
| 100 | E7 | 2637.0 | 1.25e−2 | 5 | 1 | 2 | 12 |

- **Vertical: every partial under fs/pi.** Total across E1–E7 at 48 kHz:
  **5,929 modes** (vs the Rhodes' 1,936 pedal-down). `kMaxStringModes = 132`
  (E1 needs 129). At 44.1 kHz the counts shrink naturally (E1: 101 V); above
  48 kHz the count is **capped at the 48 kHz value** [D] — partials above
  15 kHz decay in under half a second and buy nothing.
- **Horizontal: only partials below 1.3 kHz, minimum 1.** [D] The measured
  polarisation double-decay is a below-1-kHz phenomenon — the research states
  the single-alpha fit only becomes ill-defined below ~1 kHz, and every
  measured slow component sits under 300 Hz. A slow polarisation that starts
  3–9 dB down and never gets loud is inaudible against fast-decaying treble
  partials. This one decision cuts ~4,900 modes from the naive 2× duplication.

### 1.4 Polarisation parameters [M]

Per string, from the exponential fits (cp70-measured §5):

| quantity | value | measured range |
| --- | --- | --- |
| H frequency split | V × (1 + 0.75 c) | +0.5..1.0 cents |
| H initial level | −6 dB (hammer skew 0.5) | 3–9 dB below V |
| H decay ratio | alpha_V / r(m); r = 4.5 at MIDI 27 → 7.5 at MIDI ≥ 42 | 4.3 (D#1), 7.7 (F#2), 7.8–8.0 (C4) |

The r(m) interpolation follows the measured per-note ratios rather than a
constant, because the D#1 −20 dB time misses by 30% with r = 6 and lands with
r = 4.5 (section 4.2).

### 1.5 Unison detune [M][D]

Pair-centre separations measured: D3 1.05 c, A#2 1.93 c, F3 ~1.5 c, A3 ~0.5 c,
C4 5.2 c, C5 ~1.3 c — on a badly tuned 1998 rental. Default: **string 2 at
+1.2 cents nominal, per-note deterministic scatter 0.5–2.5 c** seeded from the
note number (a well-maintained CP sits at the low end; C4's 5.2 c was the
rental's worst note, not a design target). Both strings share every other
parameter.

### 1.6 Modal masses [C]

Pinned-pinned sine shapes: modal mass = μL/2 for every mode. Plain wire
(MIDI ≥ 63): μ = ρπd²/4 from the gauge map (e.g. F4: 0.975 mm → 5.86 g/m,
L = 507 mm → 1.49 g). L from the closed form `L = 665.2 mm · 2^(−(m−60)/13.16)`
(R² > 0.999). Wound bass: L interpolated log-linearly from 568.6 mm at D#4
down to **679 mm at E1** [R], μ from `T = 4 f0² L² μ` with T = 800 N (the
derived band is 730–850 N across 34 semitones) — E1: μ ≈ 256 g/m, modal mass
≈ 87 g. The bass lengths are the top-ranked unknown (section 12); the sound is
still constrained because B is measured, but hammer/string mass ratios shift
if the real lengths differ.

Memory: ~18 kB per string system × 2 × 88 ≈ 3.2 MB. Fine.

---

## 2. Mode frequencies

```
f_k = k · f0 · sqrt(1 + B(m) · k²)

B(m):  MIDI >= 63:  exp(0.0926·m − 13.64)          (treble asymptote)
       MIDI <= 46:  exp(−0.09081·m − 4.3496)       (measured CP bass law)
       47..62:      SUM of the two                  (additive blend)
```

The blend is **additive**, not a crossfade: the sum reproduces the measured
mid-range B to a few percent (D3: 2.60e−4 vs 2.55e−4 measured; A3: 3.07e−4 vs
3.31e−4; C4: 3.64e−4 vs 3.44e−4), where a linear crossfade lands at half the
measured value at D3.

**But the model's B comes from the measured anchors, not the law** [D]: the
treble law overshoots the measured points by 27–34% at C5, E6, B6 and C#7 (the
instrument sits slightly below the universal asymptote, as the research notes).
Implementation: log-linear interpolation through the 13 measured B anchors
(cp70-measured §2), with the two-segment law only as extrapolation beyond
A0/C#7. The test rows then check the model against the same numbers the
implementation consumed — which is the point: the anchors are data, the law is
a summary of it.

**Stretch tuning** [R]: the factory table (−23 c at key 1 to +35 c at key 88,
CP-80 numbering = MIDI − 20) is applied as a per-note cent offset on f0,
interpolated: E1 −13.7 c, F#2 −4.6 c, C4 ~0 c, E6 +7.2 c, E7 +20 c. This is
Yamaha's own alignment procedure, and with B this large, playing the model in
ET against the reference samples reads as out of tune. The measured Railsback
of the sampled instrument (−6 c to +40 c) confirms the shape. `tune` and pitch
bend add on top, as on the Rhodes.

Compass: authentic range **E1–E7 (MIDI 28–100)**; the engine keeps its full
A0–C8 array and extrapolates the laws outside, as the Rhodes does — the extra
notes cost nothing silent and mean out-of-range parts still play. [D]

---

## 3. Hammer

Reuse `HuntCrossleyHammer` unchanged — the model is the right one; only the
parameters change. The CP hammer is **urethane rubber over artificial leather**
[R], not felt: harder, shorter contact, and far less hysteretic than voiced
wool.

| parameter | value | reason |
| --- | --- | --- |
| alpha | **2.2**, fixed across the compass | elastomer over a crowned core is near-Hertzian; felt's load-dependent 2.5–3.5 (Stulov) does not apply |
| stiffness k | log-interpolated **8e8 (E1) → 4e10 (E7) N/m^alpha**, × the Rhodes-style `12^(hardness−0.5)` knob factor | ~2–3× equivalent felt values; produces ~1.5–2.5 ms contact in the bass, 0.3–0.5 ms treble — the "brighter excitation spectrum" the service manual's hammer choice implies |
| lambda | **0.6 s/m** | urethane's loss is a fraction of felt's; Rhodes neoprene uses 1.6–2.4 |
| mass | **11 g (E1) → 4 g (E7)**, grand-action graduation, × mass knob | the CP action is a real (shortened) grand action [R] |

- **Contact patch**: same sinc machinery as the Rhodes, identical weights for
  read and write (the reciprocity rule — filter one direction and the damper
  becomes a source; already paid for once on this project). Patch width 12 mm
  fixed [D] — the urethane crown does not change size across the keyboard, so
  the covered fraction grows toward the treble on its own, converging the
  effective mass exactly as the tine patch does.
- **Strike point**: `beta = 1/8` for MIDI ≤ 88, rising linearly to **1/6 at
  E7** [D] — the physical strike distance stays roughly constant while L
  collapses, so beta grows toward the treble (opposite of a grand's 1/10–1/12).
  **This is the one parameter with no evidence behind it** (the amplitude-fit
  measurement was attempted and proven invalid on the control instrument —
  cp70-measured §6). It is a named calibration variable, not a fact.
- **Bichord contact** [D]: one hammer, two strings — the hammer meets the mean
  of the two strings' patch displacements and the force splits equally. This
  is the only moment the unison strings interact, it is physical (they really
  do share the hammer for ~1 ms), and it vanishes at separation, so the
  measured post-strike independence is preserved.
- The explicit-contact stiffness cap (`wMax = 0.06 fs`) carries over. With
  k = 4e10 and treble modal masses ~0.3 g the cap **will** bind above ~C6;
  the diagnostic struct already reports it. If the capped treble reads too
  soft against the sample set, the fix is the quadratised contact in the
  reserved SAV slot, not a bigger cap.
- Velocity law: reuse `v = 0.18 + 5.6·vel^1.7` initially, recalibrate against
  the four velocity layers of the sample set (flagged; the exponent encodes
  Rhodes key leverage, and a grand action differs).

---

## 4. Losses and the missing soundboard

### 4.1 Per-partial decay [M]

From 979 accepted per-partial fits:

```
alpha_poly(f) = 0.393 + 9.23e−3·f − 1.275e−7·f²      dB/s, f in Hz

vertical:    alpha_V(f) = alpha_poly(f)      for f >= 1 kHz
             alpha_V    = 6.5 dB/s           for f < 1 kHz  (measured fast
                          components: 4.8–8.3 dB/s across 77–262 Hz), blended
                          800–1200 Hz
horizontal:  alpha_H = alpha_V / r(m)        r as in section 1.4
```

`setMode(i, f_k, 60/alpha, mass)` — T60 = 60/alpha. Below 1 kHz the polynomial
is the median of a fast/slow mixture and fits neither; the two-polarisation
structure replaces it there, which is exactly what the research prescribes.

### 4.2 Why the broadband numbers then fall out [C]

The −20 dB envelope times are compound, and the two-component structure
reproduces them without any further tuning:

- **C5**: fast alpha(523) = 5.2 dB/s → 3.9 s (measured 3.86 s).
- **D3**: fast at 6.5 dB/s carries the first ~8 dB in ~1.2 s, then the slow
  polarisation (starting −6 dB, 1.1 dB/s) takes over → −20 dB at ~12 s
  (measured 12.46 s; broadband T60 ≈ 62 s, the longest on the instrument, vs
  the slow component's T60 = 55–60 s).
- **E6**: alpha(1319) = 12.4 → 1.6 s (measured 1.42). **C#7**: 20.2 → 1.0 s
  (measured 1.22). **D#1** with r = 4.5: ~11 s (measured 10.28).

A single per-note decay constant cannot do this; the double-decay structure is
load-bearing, not decoration.

### 4.3 What the sustain costs

The tenor rings 2–6× longer than a 2 m grand [M] because there is no
soundboard to radiate into, and the voice must be allowed to do it. Costs,
computed from the mode table and the alpha law (modes live until 90 dB below
their initial excitation):

| scenario | live modes | vs Rhodes pedal-down (1,936) |
| --- | --- | --- |
| worst case: all 73 notes struck ff under pedal, t = 0 | **5,929** | 3.1× |
| same, after 2 s | 3,089 | 1.6× |
| after 5 s | 1,706 | 0.9× |
| after 10 s | 1,183 | 0.6× |
| after 30 s | 799 | 0.4× |

The Rhodes worst case is ~60–70% of a core, but a large share of that is the
per-voice transduction path (4× field lookups, Hermite reconstruction, SAV
rank-2 solves) — **all of which the CP-70 voice lacks**. The CP voice per
sample is: branch-free modal tick (~6 flops/mode, vectorisable, no active SAV
terms) plus one cached dot product for the bridge force. Estimate: the strike
transient lands in the same ballpark as the Rhodes worst case; **this is an
estimate, and implementation step 1 is a benchmark probe of a 6,000-mode tick
before any voice code is written** (section 13). Mitigations in order if it
misses: cap vertical partials at 12 kHz (−12%, 5,201 modes), halve string 2's
partial count above 1.3 kHz (−~15% of bichord cost), raise the shrink
threshold from −90 to −80 dB.

### 4.4 Mode shrinkage and retirement [D]

- **Shrink from the top.** alpha(f) is monotone in f over the band, so the
  highest live mode always dies first. At control rate (every 32 samples, as
  the Rhodes does) check the top mode's envelope; while it is 90 dB below its
  initial excitation, walk `numModes` down. O(1) per tick, and the parked
  modes are frozen, not cleared, so a restrike (which resets the count to
  full K) picks them up — the existing `setNumModes` semantics.
- **Retirement is relative, not absolute.** The Rhodes' absolute 1e-13/1e-10
  thresholds assume tine-scale masses; an 87 g bass string at the same
  amplitude carries orders of magnitude more energy. Retire the voice
  (`sounding = false`) at **−100 dB relative to its own post-strike peak
  energy**, and drop to a reduced mode set (the fundamental pair per string)
  at −80 dB. A ff D3 under pedal then runs ~90 s before retiring — that is
  the instrument, not a leak; the shrink schedule is what makes it affordable
  (12 modes/note × 73 ≈ 900 modes in the long tail).

---

## 5. Pickup: force sensing, not an EQ

### 5.1 The n-weighting [M]

The piezo is a charge source proportional to the transverse force at the
termination: `F = T · ∂y/∂x|₀`. For mode k of a pinned string the readout
weight is exact:

```
w_out[k] = T · (kπ / L)        vertical and horizontal alike
```

This is the **+6 dB/oct tilt as an explicit modal weighting** — the research
is emphatic that it must not be an EQ, and here it costs nothing: it is the
cached readout shape vector, the same mechanism `shapeTipV` uses. It is why
the CP is bright and fundamental-poor before any tone stack. T and L vary per
note, so absolute inter-note level differences are physical and stay in.

The readout is a pure output functional — the termination is rigid, the
sensor applies no reaction force, so reciprocity is satisfied trivially and
passivity is untouched (nothing is filtered on a feedback path).

### 5.2 Bus and corner [M][C]

- One shared **first-order high-pass at 12 Hz** on the summed bus
  (`OnePoleD`): 470 kΩ × C_bus ≈ 30 nF, inside the inferred 8–19 Hz window.
  Inaudible at E1's 41 Hz; present because it is real and it is one line.
- **No pickup resonance of any kind.** Established two ways: the pooled
  spectral residual shows no narrow fixed peak 60 Hz–15 kHz that the control
  grand does not also show, and the computed block-on-piezo resonance sits at
  12–24 kHz, at or above the band. Where `PickupCoil` sits for the Rhodes, the
  CP-70 has nothing — `coilFreq`/`coilQ` are hidden for this instrument.
- **Region trims** [R][D]: the CP-70 bus is six blocks joined by 4.7 kΩ mixing
  resistors (between keys 63/64 and 65/66); the CP-80 uses a 0.022 µF divider
  above key 82. Yamaha balancing register levels. Modelled as a per-note gain
  table with breakpoints at the documented keys; values from a nodal solve of
  the bus network with C_element = 300 pF (open question 10; start at 0 dB and
  fit against the sample set's per-note levels).

### 5.3 No per-string saturation [D]

The Rhodes needs per-tine core saturation because each tine has its own iron
and the flux nonlinearity is per-note physics. The CP-70 has no magnetics:
PZT charge is linear in stress to well under 1% at these force levels, and
the measurements say so directly — the D3 beat nulls reach −42 dB (a
memoryless per-note nonlinearity ahead of the sum would still preserve nulls,
but any level-dependent gain would not track two decaying strings to −42 dB),
and the pooled spectra show no per-note distortion signature. **The voice
output is a linear functional of the modal state.** Consequences:

- No per-voice tanh, no field lookup, no per-voice oversampling — the entire
  `kOver` flux path disappears. The voice emits one force sample per sample.
- The only nonlinearity in the CP-70 signal path is the shared JFET preamp
  (section 6), which is where a chord should intermodulate — mildly.
- The output chain for this instrument runs at **base rate** [D]: the JFET
  stage at its default drive contributes less than −60 dB of harmonics, and
  what it makes above Nyquist from an already band-limited sum is negligible.
  A test row measures folded-alias residue and must show < −70 dB before this
  decision stands (the spice lesson: measure alias rejection, never assume it).

---

## 6. Preamp, tone, tremolo

### 6.1 The mid scoop [R]

SPICE of the real cut-only tone stack, controls flat, relative to 10 kHz:
20 Hz ≈ +2.5 dB, 100 Hz ≈ −7.5 dB, 500 Hz ≈ −14.5 dB — a **14–15 dB scoop
centred 500–600 Hz** with the low bass above the 10 kHz level. This is the
most under-reported fact about the instrument and it ships as a **fixed
filter**, not a user curve:

- peaking cut, fc = 560 Hz, −15 dB, Q ≈ 0.55
- low shelf +9 dB below ~55 Hz
- high shelf +3 dB above ~3.5 kHz

fitted to the anchor points within ±1.5 dB (a dedicated test row measures the
filter itself, section 10). The broad 2.2–9 kHz lift in the pooled sample
residual is the tone stack **plus** the force tilt — the tilt is already
explicit in 5.1, so the filter is fit to the SPICE anchors only, never to the
pooled residual (that would double-count).

- User `bass`/`treble` map to the documented control ranges: bass ±17 dB at
  50 Hz, treble ±12.5 dB at 5 kHz (the real controls are cut-only; the boost
  half is a studio convenience and says so in the UI). No brilliance switch —
  that is CP-80 only [R].
- After the stack: one soft asymmetric class-A JFET stage (2SK30A input, nine
  discrete stages, no op-amp [R]) on the `preampDrive` control, default nearly
  clean.

### 6.2 Tremolo [R][D]

0.8–10 Hz LDR auto-panner, two channels driven in antiphase — the same family
as the Suitcase vibrato, and `SuitcaseVibrato` is reused as-is: its photocell
attack/decay asymmetry (2.5/35 ms) is the LDR physics the CP shares, and
`stereo = 1` is exactly the antiphase wiring ("one XLR for each phase of the
Tremolo"). The `tremRate` parameter already spans 0.1–12 Hz and the DSP clamp
0.1–30 Hz, so the range is covered with no change. Depth spec (>40% max,
<15% min) is inside the existing mapping. The CP LFO is an IC oscillator
rather than the Janus trapezoid; if A/B against reference recordings shows the
edge shape, a shape parameter on the shared class is the fix — not a second
implementation.

---

## 7. Dampers stop at A6

[R] Both models damp only up to **A6 = MIDI 93** (CP-70 key 66); the top
octave rings free, as on an acoustic grand.

- `CP70Voice::applyDamper` is gated: notes above 93 ignore key-up and pedal
  state entirely — `noteOff` clears `held` but the voice keeps sounding until
  energy retirement. The engine needs no change: the damper is already applied
  inside the voice, so the gate is one line where `damperFactor` is applied.
- Below A6: same `scaleMode` damper as the Rhodes, grip graduated heavier in
  the bass. Damper release behaviour (half-pedal, re-grab of a ringing string)
  is research unknown #6 — launch with the Rhodes semantics, flag it.
- Repeated-note and retune semantics (fade-then-strike, state never cleared on
  same-note restrike) carry over unchanged — they are voice-lifecycle logic,
  not Rhodes physics.

---

## 8. Engine integration

### 8.1 The instrument selector returns

Removed because a one-entry `AudioParameterChoice` has range [0, 0] and AU
validation reads back NaN from the normalisation divide. With two real
instruments it comes back:

- `instrumentNames { "Rhodes", "CP-70" }`, **appended at the END of the
  parameter layout** — the layout comment is explicit that hosts address by
  position and `testParameterOrderIsStable` pins it. It was never published,
  so appending is not a break.
- Choice index maps through a table `{ Instrument::rhodes, Instrument::cp70 }`
  rather than casting — `enum Instrument` keeps its four-entry order (it is
  shared with `epi::ids` documentation, and wurlitzer/clavinet slot in later
  without renumbering).
- Switching instruments is a full `reset()` + rebuild, not a crossfade: it
  changes what the voices are. An audible gap is correct.

### 8.2 Two voice arrays, one active [D]

`std::array<RhodesVoice, 88>` stays; `std::array<CP70Voice, 88>` joins it
(~3.2 MB, section 1.6). The process loop dispatches once per block on the
instrument — no per-sample virtual calls, and each path keeps its own shape
(the Rhodes needs the oversampled flux sum and Decimator; the CP-70 sums
forces at base rate and skips both). Shared: event handling, keyDown/pedal
state, ActionNoise, Room, vibrato, output gain, telemetry atomics.

### 8.3 Parameter mapping

| param | Rhodes | CP-70 |
| --- | --- | --- |
| velCurve, hammerHard, hammerMass, escapement, strikeNoise, damperGrip | as now | same roles (hammer k factor, mass factor, let-off, action thump, grip) |
| tipMass ("Tuning Spring") | spring position | **unison detune spread** 0–5 c (0.5 default ≈ 1.2 c) — the only honest CP meaning for a per-note tuning control |
| resDamp | tine Q trim | global alpha trim ×0.7–1.5 |
| barCouple, barTune | tonebar | **hidden** — no tonebar exists |
| bodyMix | harp coupling | **hidden, coupling forced to 0** — see 8.4 |
| nonlinAmt ("Bloom") | stretch term | **hidden** at launch (open question 8) |
| pickupPos, pickupDist, coilFreq, coilQ, coilSat | magnetic voicing | **hidden** — no magnetics; nothing is repurposed into a fake control |
| preampDrive, bass, treble | Suitcase | CP tone stack + JFET (section 6) |
| tremRate/Depth/Stereo | Suitcase panner | CP panner, default stereo = 1 |
| cabMix | Rhodes cab | usable but default 0 in CP presets — a CP-70 went to the PA |
| spaceMix/Size, outGain, tune | shared | shared |

Hiding is the UI's job (`panels.jsx` per-instrument panels); the DSP simply
does not read the hidden ones in the CP path, and the "every control does
something" test runs per instrument against its visible set.

### 8.4 No cross-string coupling, at all [M][D]

The Rhodes harp path exists because the measured instrument answers
sympathetically through its frame. The CP-70's measured behaviour is the
opposite: rigid per-note terminations, pure superposition, −42 dB beat nulls,
no prompt/aftersound. **The CP path sets harp coupling to zero and does not
route string state through `Harp` — sympathetic resonance on this instrument
would be an invented feature contradicted by the data.** The frame still
carries the action thump (`ActionNoise` output mixed dry, not injected into
string clamps), because key knock is audible on the recordings and travels
through the case, not through string coupling.

---

## 9. What CP70Voice does per sample

```
1. hammer.tick against patch-weighted mean of string displacements (if active)
   -> force split into both strings' V modes (skew 0.5 into H)
2. per string: sys.tick()            // pure linear, no SAV terms active
3. bridge force = sum over strings, both polarisations, of w_out · q
4. return one double                 // no oversampling, no field, no tanh
5. at control rate: top-mode shrink check, retirement check, damper gate
```

Engine CP path per sample: sum voice forces → region trim already folded into
w_out → 12 Hz HP → tone stack biquads → JFET stage → vibrato panner → room.

---

## 10. Test plan

`tests/test_cp70_reference.cpp`, same row/verdict machinery as
`test_epi_reference.cpp`, backed by a `docs/cp70-checklist.md` once numbers
stabilise. Reference chain for all spectral rows: tone controls flat, tremolo
off, room off (matching the sample set's LTI chain). Targets from
cp70-measured.md:

| # | Property | Target | Method |
| --- | --- | --- | --- |
| K1 | B at 8 notes (D#1, B1, F#2, D3, A3, F4, C5, E6) | measured table ±15% | peak-fit partials 1–8, LSQ fit of f_k/k f0 vs k² |
| K2 | Partial frequencies follow f_k law | ±3 cents to k = 8 | same fit, residual |
| K3 | Stretch tuning | E1 −13.7 c, E7 +20 c, ±3 c | fundamental vs ET |
| T1 | −20 dB time at 6 notes | D#1 10.3, D3 12.5, C5 3.9, G#5 2.2, E6 1.4, C#7 1.2 s, ±30% | beat-suppressed upper envelope, as the research measured it |
| T2 | C4/D#4 still < 20 dB down at 7 s | yes | same |
| T3 | Tenor rings 2–6× a grand | ratio vs stored C5-grand values, informational | same |
| D1 | Double decay, single-string note (F#2) | 2-exponential fit, ratio 4–8, slow starts 3–9 dB down | complex-exponential fit on fundamental baseband |
| D2 | Same structure at D#1 | ratio 4–8 | same |
| U1 | Unison beat nulls (D3) | ≤ −35 dB (measured −42) | envelope minima vs peak |
| U2 | Two matched pairs at C4 | 4 components, pair split 0.5–1.5 c, centres 1–5 c | 4-exponential fit |
| U3 | Superposition is exact | render string 1 only + string 2 only, sum, diff vs bichord render < −80 dB | proves no accidental coupling in code |
| P1 | Force tilt is n-weighting | drive known modal state, partial ratio = k ratio ±0.1 dB | unit-level, no hammer |
| P2 | Bus high-pass corner | 12 Hz ±4 | swept response |
| P3 | No resonance 60 Hz–15 kHz | pooled residual < ±5 dB broad, no narrow peak | mirror of the research method |
| P4 | Alias residue at base rate | < −70 dB | Goertzel at folded bins, hard note + full drive (gate for the no-oversampling decision) |
| S1 | Mid scoop | 500 Hz −14.5, 100 Hz −7.5, 20 Hz +2.5 dB rel 10 kHz, ±1.5 | filter response direct |
| S2 | Bass-poor D#1 | fundamental 20.6 dB below partial 4, ±4 dB | spectrum at 0.5 s — **the calibration row for bass beta and hammer k** (see below) |
| A1 | Contact duration | ~1.5–2.5 ms bass → 0.3–0.5 ms treble, informational until measured | `contactSamples` |
| V1 | Tremolo antiphase | L/R gain correlation < −0.9 at stereo = 1; 0.8 and 10 Hz reachable | telemetry |
| G1 | Damper gate | B6 rings on after noteOff (T60 unchanged); A6 damps | envelope pre/post release |
| G2 | Undamped top + pedal irrelevant above A6 | same decay with and without pedal | same |
| C1 | Adversarial CPU | < 0.5% missed deadlines at 48 kHz, 73-note pedal glissando | existing stress harness |
| C2 | Retirement | ff D3 under pedal retires (no immortal voices), > 60 s allowed | energy trace |
| N1 | No NaN/energy growth | energy never rises in any linear segment | existing S-row machinery |

On S2: a flat-force-spectrum excitation with beta = 1/8 predicts only ~+2 dB
for partial 4 over the fundamental after the scoop; the measured +20.6 dB
needs some combination of smaller bass beta, harder contact, and the wound
string's real excitation. The row is expected to FAIL first and drive the
bass calibration — that is its job, and the plan says so now rather than
after the surprise.

---

## 11. What is deliberately not in this plan

- **Soundboard, radiation, cabinet coupling** — the instrument has none, and
  the decay data confirms the consequence.
- **Inter-string and inter-note coupling** — measured absent (section 8.4).
- **String nonlinearity (tension modulation / phantom partials)** — no trace
  in the measured sample analysis; the SAV slot is reserved if the ff bass
  says otherwise (open question 8).
- **Pickup resonance** — measured absent two ways.
- **CP-80 compass and brilliance switch** — this is the CP-70; the CP-80 is a
  preset away (88 keys, brilliance, capacitor bus trim) once the CP-70 rows
  pass, and not before.

---

## 12. Open questions, ranked by risk

| # | Unknown | Risk | Closes with |
| --- | --- | --- | --- |
| 1 | Worst-case CPU of a ~6,000-mode tick | High — the whole voice sizing rests on it | The step-1 benchmark probe; mitigations pre-ranked in 4.3 |
| 2 | Wound-bass speaking lengths / μ (MIDI 28–62) | High for mechanism, medium for sound (B is measured either way) | A tape measure on one real CP-70, or one scaled photo of the harp |
| 3 | Strike point beta, especially bass | Medium-high — owns the attack comb and row S2 | Measure hammer line to capo on a real instrument; until then S2 calibrates it |
| 4 | Vertical fast-alpha below 1 kHz (constant 6.5 dB/s rests on 4 notes) | Medium | More per-note exponential fits from the existing cp70b analysis scripts, MIDI 43–70 |
| 5 | Urethane Hunt-Crossley parameters | Medium — attack brightness | Fit contact duration/brightness against the 4 velocity layers per note |
| 6 | Damper release behaviour | Medium for playability | Separate recording task (research unknown #6) |
| 7 | Whether ff bass shows phantom partials (string NL) | Low-medium | Scan Sullivan ff bass samples for sum/difference partials; if present, Kirchhoff term in the reserved SAV slot |
| 8 | Region trim values (mixing-resistor bus) | Low | Nodal solve with C_element 300 pF; fit residual per-note levels |
| 9 | Piezo corner inside 8–19 Hz | Low — inaudible at E1 | One LCR reading on a real element |
| 10 | Tremolo LFO edge shape vs Janus trapezoid | Low | A/B against any CP-70 recording with tremolo up |

---

## 13. Implementation order

1. **Benchmark probe**: a bare `SavModalSystem` tick at the section-1 mode
   census on the target machine, worst case and 5 s-decayed case. Gate: fits
   the 48 kHz budget with the margin the Rhodes leaves. (Everything else
   waits on this number.)
2. `CP70String` + `CP70Voice`: mode laws, polarisations, hammer, patch,
   shrink/retire. Unit rows K1–K3, D1–D2, U1–U3, P1.
3. Bus + preamp + tremolo wiring, base-rate path. Rows P2–P4, S1, V1.
4. Engine dispatch, selector at end of layout, per-instrument parameter
   visibility, damper gate. Rows G1–G2, C1–C2, N1.
5. Calibration pass against the sample set: S2 (bass beta / hammer k), A1,
   T1–T3, per-note level trims. This is where the evidence-free parameters
   earn their values, with the suite as judge — the Rhodes striking-line
   lesson says calibrate the coupled ones together, not one row at a time.
6. Presets ("Studio 1977 DI", "Stage + tremolo"), UI panel, checklist doc.

Sources: `docs/research/cp70-measured.md` and `docs/research/yamaha-cp70-cp80.md`
(which carry the primary-document citations), plus the measured Rhodes
architecture in `src/epi/dsp/` that this reuses.
