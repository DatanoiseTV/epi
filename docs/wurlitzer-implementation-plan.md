# Wurlitzer 200A implementation plan

How the Wurlitzer 200A goes into this engine. Tags: **[M]** measured in `docs/research/wurlitzer-200a.md`, **[C]**
computed, **[R]** read in a primary document, **[D]** a design decision with its reason. Companions: that file,
`docs/research/transducers-and-chassis.md` §3/§5.1, `openwurli` (reference implementation, reviewed at source level
for this plan), Pfeifle DAFx-17 §4.3/5.5/5.6.

The one-sentence version: a Wurlitzer note is **one flapwise mode of a solder-loaded steel reed** — measured harmonic
to ±0.1 cent to the 20th partial, which no cantilever mode series can be [M] — read by an electrostatic pickup whose
1/(gap−y) law manufactures every harmonic, summed as per-reed capacitance perturbations onto one stiff 240 pF node,
filtered by one shared ~2.3 kHz RC highpass, then a two-transistor preamp, an optocoupler amplitude tremolo and two
small open-back speakers. Same shape as the Rhodes — the resonator supplies frequency and envelope, the transducer the
timbre — with the field table swapped for a capacitance law. **A modal bank at harmonic ratios would double-count**
[M], and per-partial decay must emerge, never be parameterised (measured harmonic-k decay ratios 1.58–6.28 for k =
2..8 [M]).

---

## 1. Voice structure

### 1.1 Shape

```
hammer -> reed (1 polarisation, <=3 modes + beat partner) -> y = u_tip/gap
             +-> DC_n = C0n * y/(1-y)     (per-reed, memoryless, soft knee)
                     |
   64 voices --------+--> one bus -> shared RC highpass ~2.3 kHz (at 4x)
                            -> 200A preamp -> tremolo -> speakers -> out
```

`WurliVoice` = one `SavModalSystem<kReedModes, 2>` + one `HuntCrossleyHammer`. **No SAV term active at launch** [D]:
the spectra show no amplitude-dependent pitch structure (harmonicity holds "even under extreme playing conditions",
DAFx-17 §5.5 [R]); the slots are reserved for a quadratised contact if the treble stiffness cap binds. Voice output is
the reed **tip displacement**; transduction lives engine-side (it needs the oversampled reconstruction, §5.5).

### 1.2 One polarisation, argued from geometry [C][D]

The tine is round wire with degenerate polarisations; the reed is a **flat strip**: widths 0.151 in (bass) → 0.096 in
(treble), tongue 0.020 in early / 0.026 in late [R]. Width/thickness = 4.8–7.5, and bending frequency scales with
thickness in the bending direction, so the edgewise fundamental sits **5–7× above** flapwise [C]: no near-degenerate
pair to beat. Edgewise motion also runs *along* the symmetric slot edges, where dC/dy vanishes at centre by symmetry
[C]. **One polarisation.** DAFx-17 does the same and says why: plate effects "were not measured", coupling omitted
[R].

The measured **~2.4 Hz AM** on every harmonic [M] therefore cannot be a polarisation beat. It ships as a **beat
partner** [D]: a second flapwise component at f0 + δ, δ default 2.4 Hz, starting −12 dB, in the slot the Rhodes gives
to H. Phenomenological but measured — a static sine sounds dead without it [M] — and flagged: open question 4 names
the measurement (sideband spacing vs harmonic index) that decides between two beating mechanical components (spacing
k·δ) and global gain modulation (constant δ).

### 1.3 Mode count and masses [M][C][D]

The sustained sound needs one mode; the attack carries a brief inharmonic chirp (modes 2–3, heavily suppressed:
nothing above −45 dB between 3× and 25× f0 [M]; openwurli calibrated mechanical mode 2 to −43 dB against recordings).

```
kReedModes = 4: [0] fundamental  [1] beat partner (f0+delta)
                [2] mode 2       [3] mode 3
```

Modes 2–3 enter at the level the patch + dwell machinery produces (the same sinc + dwell suppression as the tine) and
are refused by `setMode` above the fs/π budget:

| note | f0 (Hz) | µ solved (§2) | f2/f1 [C] | f2 (Hz) | f3 (Hz) | modes @48k |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | 55 | ~0.19 | 6.9 | 380 | 1160 | 4 |
| A3 | 220 | ~0.05 | 6.4 | 1410 | 4200 | 4 |
| A5 | 880 | ~0.01 | 6.3 | 5540 | 15500 | 3 |
| C7 | 2093 | ~0.01 | 6.3 | 13160 | — | 3 |

Worst case 64 × 4 = **256 modes**, 13% of the Rhodes' 1,936 pedal-down [C].

Modal masses (tip-normalised ρAL/4, the engine's convention; steel 7850 kg/m³, free length per §2.2): reed 1 (A1),
3.835 × 0.508 mm section → beam ≈ 1.15 g, modal ≈ **0.29 g**; reed 64 (C7) → beam ≈ 0.25 g, modal ≈ **0.06 g** [C];
plus solder µ·ρAL. Same order as the tines, but retirement thresholds ship **relative** (−100 dB below post-strike
peak energy; reduced set at −80 dB) — the CP-70 plan's better practice [D].

---

## 2. The reed: tuning IS solder mass

### 2.1 The characteristic equation, solved fresh [C]

openwurli's β₂ table is wrong — 18% high at µ = 0.5, no clamped-pinned convergence [M] — so the eigenvalues were
re-solved for this plan from

```
1 + cos(β)cosh(β) + β·µ·(cos(β)sinh(β) − sin(β)cosh(β)) = 0
```

verified both ways: µ = 0.5 → f2/f1 = 8.382 (the research doc's stated correct value); µ → ∞ → β₁ = 3.9266
(clamped-pinned limit).

| µ | β₁ | β₂ | f2/f1 | f3/f1 |
| --- | --- | --- | --- | --- |
| 0.0 | 1.8751 | 4.6941 | 6.267 | 17.55 |
| 0.1 | 1.7227 | 4.3995 | 6.522 | 18.71 |
| 0.5 | 1.4200 | 4.1111 | 8.382 | 25.64 |
| 1.0 | 1.2479 | 4.0311 | 10.44 | 32.68 |
| 1.7 | 1.1156 | 3.9916 | 12.80 | 40.60 |

Tip mass pushes partials **away** from harmonic — no µ makes a reed harmonic [C], which is the architecture's licence.
Implementation: one bisection per note at prepare().

### 2.2 Geometry, and the two-unknown solve [R][C][D]

Catalog lengths follow an exact ruler law [R]: reed 1 = 2 19/20 in, each 1/20 in shorter to reed 20 = 2 in; reed 21 =
1 43/44 in, each 1/44 in shorter to reed 64 = 1 in. The research doc's µ trend (~1.7 bass → 0.1 treble) assumes
catalog length = free length and uniform thickness; the absolute pitch check refuses that: a 74.9 mm tongue at 0.020
in rings bare at ~74 Hz, so A1 = 55 Hz needs only **µ ≈ 0.19** [C]; and a 25.4 mm tongue at 0.020 in rings bare at
~640 Hz — C7 = 2093 Hz is **unreachable by any µ ≥ 0** [C].

So both assumptions fail together: part of the catalog length is clamped under the reed-bar screw, and the tongues are
ground. Resolution [D]: treat **clamp offset** (one global calibration length, order 10 mm) and **tongue thickness**
(0.020/0.026 in generation switch) as the calibration pair, then **solve µ per note from the pitch** — the reed lands
on its note by construction, as the Rhodes solves tine length, and µ stops being free. More solder = lower pitch is
automatic; master tune and pitch bend apply by **re-solving µ** (the tech's add-or-file-solder move), not by a
frequency offset. The attack-chirp measurement (open question 2) closes the pair.

### 2.3 The tipMass knob: the thickness–solder trade [C][D]

At fixed pitch, thickness and solder trade: the bass reed at t = 0.020 in needs µ ≈ 0.19 (f2/f1 ≈ 6.9); at 0.026 in, µ
≈ 0.50 (f2/f1 ≈ 8.4) [C] — the real difference between early and late 200A reeds [R]. `tipMass` maps to tongue
thickness 0.020 → 0.026 in with µ re-solved per note: pitch stays put; attack chirp, strike mass and contact all
shift. A voicing control with a factory referent, not a fake.

---

## 3. Losses

### 3.1 One Q, anchored per note [M][D]

Q ≈ 1000 flat (median ~970, mostly 860–1060): hysteretic material + clamp loss [M]. The model consumes the **anchors,
not the law** (the CP-70 rule):

| f0 (Hz) | dB/s [M] | T60 [C] | implied Q [C] |
| --- | --- | --- | --- |
| 55 | 1.11 | 54 s | 1350 (only 1.4 dB fitted — low weight) |
| 124 | 3.40 | 17.6 s | 995 |
| 165 | 7.94 | 7.6 s | 567 |
| 277 | 7.39 | 8.1 s | 1020 |
| 553 | 14.4 | 4.2 s | 1050 |
| 787 | 25.4 | 2.4 s | 846 |
| 1664 | 21.3 | 2.8 s | 2130 |

Log-interpolate dB/s through the anchors; extrapolate with Q = 1000 [D]. The scatter (567–2130) is real per-reed
variation: a deterministic ±0.2 log-Q jitter seeded from the note number is truthful and avoids the unnatural evenness
of a constant law [D]. Modes 2–3 have no measured Q (below −45 dB); give them Q/15 [D] — the Rhodes'
fundamental-vs-overtone ratio logic — so the chirp dies in tens of ms.

### 3.2 The sustain control is clamp loss [M]

Filing the reed-bar knife edge changes sustain by seconds [M], so `resDamp` scales the anchored Q ×0.5–1.5 and is
labelled as clamp loss. Per-partial decay is **never** parameterised: harmonic k decays k× faster automatically as a
function of one decaying sinusoid; the measured ratios (1.58, 2.60, 3.03, 3.83, 4.10, 5.27, 6.28 for k = 2..8 [M]) are
a test row, not an input.

---

## 4. Hammer, action, dampers

### 4.1 Felt, Hunt-Crossley, and why not Stulov [R][D]

The 200A hammer is 3-ply maple with mothproofed **felt** [R], not neoprene. Felt: F = k·x^p, p ≈ 2.5–3.5, strongly
hysteretic (Hall/Askenfelt via the openwurli research file [R]). Stulov adds a memory kernel; Hunt-Crossley captures
the hysteresis loop via λ·d^α·ḋ without one. **Reuse `HuntCrossleyHammer` with felt parameters** [D]:

| parameter | value | reason |
| --- | --- | --- |
| alpha | **2.6** | low end of felt's 2.5–3.5 for thin voiced felt [R] |
| stiffness k | calibrated per note to the Miessner contact target, × `12^(hardness−0.5)` knob | the observable is contact time, not k |
| lambda | **3.5 s/m** | felt hysteresis exceeds neoprene's 1.6–2.4 [D] |
| mass | ~4 g bass → ~2 g treble [D], graduated against effective reed mass as the Rhodes does | small maple hammers; no primary figure — flagged |

Justification: DAFx-17 uses Hunt-Crossley for this very instrument ("good results for moderate impact velocities"
[R]); the observables are contact time and attack spectrum (rows A1/K4), and if those fail, Stulov is the named
escalation.

**The calibration target is a patent** [R]: Miessner US 2,932,231 — contact lasts "three fourths to one cycle of
vibration at its fundamental frequency": A1 ≈ 13.6–18.2 ms, C4 ≈ 2.9–3.8 ms, C6 ≈ 0.72–0.95 ms [C]. A ~0.9-cycle
contact is itself most of why the upper modes stay quiet (the dwell argument the Rhodes already implements).

**Contact patch**: identical sinc weights for read and write (the reciprocity rule this project already paid for).
Width 6 mm fixed [D] — the felt crown is smaller than the Rhodes tip; the covered fraction grows toward the treble as
reeds shorten. **Strike point**: no primary figure; the blow is against the underside of the reed [R]. Start β = 0.20
of free length, constant [D] — a named calibration variable owned by the attack-spectrum row, like the CP-70's S2.

### 4.2 Velocity mapping from the action geometry [R][C]

Manual pp. 16–19 [R]: blow distance 30.95 mm, let-off 3.18 mm, key dip 9.53 mm. Lever ratio ≈ 30.95/9.53 ≈ **3.25**
[C]; the hammer free-flies the last 3.18 mm, so escapement reuses the Rhodes machinery with `escMm` fixed at 3.18 mm ×
knob factor [D]. Launch with v = 0.18 + 5.6·vel^1.7, recalibrate against the measured pp→ff harmonic growth (row V1).

### 4.3 Dampers [R][D]

Felt dampers, rest clearance ~0.79 mm, damper gap 0.035 in [R]. Reuse the `scaleMode` damper, grip graduated heavier
in the bass. **No evidence of an undamped top range found — assume all 64 damped** [D], flagged (open question 8).
Repeated-note semantics carry over unchanged.

---

## 5. The electrostatic pickup

### 5.1 The law, and the real topology [R][M][C]

i = u₀·dC/dt with C(y) = C₀/(1−y), y = displacement/rest gap (DAFx-17 §5.6, parallel-plate limit of the FEM
capacitance) [R]. The reed vibrates through **symmetric cutouts in one shared comb plate** ("vibrate freely between
the symmetric cutouts" [R]; corroborated by the manual's "off center **in** the pickup" voicing language and the "reed
shorted against pickup" fault entry [R]). All 64 gaps hang on **one node** behind one resistor network; per-reed rest
capacitance ~2–4 pF of a ~240 pF node [C geometry; C_total single-sourced — §5.3].

That geometry decides the factoring [C][D]. One reed modulates the node by C₀ₙ/C_total ≈ 1.3% at rest, ~12% at y = 0.9
— the node is a stiff voltage source to any single reed. Linearising the node equation in the signal (u₀ ≈ 150 V vs
~1.8 V of signal) but keeping y exact:

```
v_out = −(u₀/C_total) · H(s) · SUM_n DC_n,   DC_n = C0n · y_n/(1−y_n)
H(s)  = sRC_total / (1 + sRC_total)
```

**Per-reed memoryless nonlinearity → linear sum → one shared first-order highpass.** openwurli instead couples a
time-varying RC per voice, which implicitly gives each reed the full 240 pF (100% modulation) — right for a lone reed
on its own plate, not for a comb. The shared-node form is cheaper (no per-voice filter state), makes chord behaviour
exact (superposition at the node; intermodulation belongs to the preamp, where the circuit puts it), and keeps the
measured frequency shaping — H2/H1 still falls above the corner because H(2f)/H(f) → 1 there. The two models disagree
in one place: openwurli's constant-charge regime predicts asymmetry vanishing above the corner; the shared node keeps
y/(1−y) sensing at all frequencies. Row P5 plus the measured treble tables arbitrate. Per-reed C₀ₙ graduation (bass ≈
3.5 pF → treble ≈ 2 pF [C]) folds into a per-note output weight. Check [C]: u₀·C₀ₙ/C_total = 147 × 3/240 = 1.84 V —
exactly openwurli's `PICKUP_SENSITIVITY`.

### 5.2 The asymmetry IS the bark [M]

y/(1−y) amplifies motion toward the plate more than away: even harmonics rise with amplitude. **Drive it with
displacement relative to the gap and let the register dependence emerge** [M]: measured H2–12 energy vs fundamental
runs A1 pp −4.9 → ff **+26.7 dB** (~32 dB growth), E3 f +6.6, Db4 f −3.6, Db5 f −12.2 dB — the bass barks, the treble
never does, from one law. No per-key hand-tuning.

### 5.3 The corner: 2312 vs 2834 Hz, resolved [R][C]

The two figures belong to two different instruments, and one was never a coherent circuit:

- **2312 Hz** = (1 M ‖ 402 k) × 240 pF = 287 kΩ × 240 pF. The 1 MΩ feed is **component 56 in the 200A's HV filter
  chain, read from 200A schematic #203720-S-3** (serial 102905+) by openwurli, whose circuit docs are
  schematic-extracted with ngspice cross-checks and explicitly distinguish Avenson's 499 kΩ *replacement* design from
  the original [R]. The 402 kΩ is the 200A input network (22 k + 380 k) [R].
- **2834 Hz** pairs the **Series 200 manual's** R-56 = 560 kΩ [R] with the **200A's** 402 kΩ input network — a chimera
  of two electrically different instruments (different rails, preamp, tremolo [M]). The Series 200's own corner would
  need the Series 200's input network, which nobody computed.

**Verdict: 2312 Hz for the 200A** [R+C]; the 560 k figure belongs to the other instrument. The honest residual
uncertainty is **C_total = 240 pF**: a single forum measurement, adversarially refuted as unverifiable by openwurli's
own research pass, retained because the geometric estimate (64 × 2–4 pF + wiring ≈ 130–250 pF) is consistent [C].
Corner range under that: ~2.2–4.3 kHz. One LCR reading of a real reed bar closes it (open question 1). R and C stay
internal constants, not knobs.

### 5.4 Bound analysis: the knee is physics, not a hack [R][C][D]

1/(1−y) diverges as y → 1, but the parallel-plate law expires first: in the slotted comb the reed can pass **through**
the plate plane — the FEM capacitance rises toward the plane, turns over, and falls beyond (DAFx-17 computes C by FEM
precisely because the parallel-plate form fails there) [R]. Physical C(y) is bounded and smooth; the stand-in is a
**C¹ soft knee**: identity below y = 0.94, tanh bend asymptotic to 0.98 [D — openwurli's reviewed fix; its earlier
hard clamp measurably sprayed broadband hash]; plate contact is a fault, not an operating point [R]. The gap is
undocumented — confirmed absent from the manual [M]; 0.5 mm assumed (Pfeifle), the per-note y-scale owned by the V1
calibration.

### 5.5 Oversampling: yes, 4×, reusing what exists [C][D]

Reed motion is band-limited (one mode); y/(1−y), slope up to 1/(1−0.94)² ≈ 278 at the knee, makes harmonics without
practical limit, and on a 55 Hz bark the folded content lands mid-band. Identical problem and machinery to the Rhodes
field [D]: mechanics at base rate, Hermite tip-path reconstruction at kOver = 4, ΔC per subsample, shared HP + preamp
at 4×, one decimation through the existing 16th-order Butterworth. Row P4 gates at −70 dB — measured, never assumed.

### 5.6 Bias voltage as a parameter; no back-action [R][C][D]

`biasVolts` 130–170 V, default **150** [R — the 200A rail is +150 V nominal per its assembly drawing; Pfeifle measured
a sagging unit at 130 V; 160–170 V is the Series 200's hotter rail, inside the range deliberately and labelled]. It
scales sensitivity linearly — a physical level/drive-into-preamp control. Back-action stays unmodelled [M]: softening
−0.048 N/m vs 539 N/m mechanical (−0.077 cents), load power 0.03% of stored energy at 0.5 mm. Softening ∝ 1/d³ (~1.2
cents at 0.2 mm), so **the gap knob floors at 0.3 mm** [D], keeping the pure-sensor claim true.

---

## 6. Preamp, tremolo, speakers

### 6.1 Solid state, definitively [R]

The 200A is transistors end to end: two-transistor reed-bar preamp, twin-T oscillator, LED/LDR optocoupler,
quasi-complementary class-AB output, +15 V LV rail [R]. Tube Wurlitzers are the earlier 100-series; no tube stage is
modelled or missed. Output-pair crossover distortion is a flagged refinement, not launch scope [D].

### 6.2 WurliPreamp [R][C][D]

Not the Suitcase circuit; a small dedicated stage [D]: input coupling highpass ~18 Hz (0.022 µF into 402 kΩ [C]);
bandwidth lowpass ~15.5 kHz (the 100 pF Miller caps [R]); one asymmetric soft-clip stage with headroom ~2 V toward
saturation vs ~11 V toward cutoff [R], implemented as the Rhodes-style asymmetric tanh with that 2:11 split [D];
no-vibrato gain ≈ 14 dB [C — openwurli's corrected divider analysis, agreeing with Avenson's ~15 dB measurement].
`preampDrive` scales into the knee exponentially, default nearly clean. `bass`/`treble` remain a studio convenience
and say so in the UI — **the real 200A has no tone controls** [R], only volume and vibrato.

### 6.3 Tremolo: amplitude, optocoupler, twin-T [R][C][D]

The 200A tremolo modulates **gain** (LED/LDR shunting a feedback divider), not pan. Checked in code:
`SuitcaseVibrato::setStereo(0)` makes tgtA = tgtB with identical envelope dynamics — L and R exactly equal, already a
true amplitude tremolo — and its photocell constants (2.5/35 ms) are the VTL5C-class figures of the 200A's LG-1 opto
[R]. Two deltas [D]:

1. **LFO shape**: the class hardcodes the Janus trapezoid; the 200A twin-T is near-sinusoidal at ~5.3–5.6 Hz [R/C —
   openwurli's ngspice-validated circuit]. Add a trapezoid↔sine shape blend on the shared class — one parameter, not a
   second implementation (the CP-70 plan's rule).
2. **Depth**: full depth ≈ **7.3 dB peak-to-peak** of gain [C — openwurli Rust 7.33 dB vs ngspice 7.31 dB on the real
   divider network]; map `tremDepth` = 1 to that; row M2 measures it.

The famous 5.75 Hz belongs to the **Series 200's** phase-shift oscillator — do not cross-apply [M]; Wurlitzer presets
default 5.6 Hz (inside `tremRate`'s span). Flagged, not shipped: the LDR sits inside the preamp feedback, so gain,
bandwidth (~0.8 dB at 10 kHz) and distortion breathe together [R]; launch as pure gain AM, revisit on A/B evidence.

### 6.4 Speakers [R][D]

Two 4×8 in oval ceramic-magnet speakers in the open-back lid [R]. Reuse the `Cabinet` class shape with Wurlitzer
corners [D]: highpass ~95 Hz, Q ≈ 0.75 (open-baffle cancellation + driver resonance bump), lowpass ~5.5 kHz
(openwurli's A/B against recordings puts the treble centroid there, not at the nominal 7.5 k [C]), existing presence
bump and tanh excursion limit. `cabMix` defaults **0.7** in Wurlitzer presets [D]: the onboard speakers are the
canonical 200A sound; `cabMix` = 0 is the (also real) aux/DI path.

---

## 7. Engine integration

### 7.1 The selector returns [R][D]

It was removed because a one-entry `AudioParameterChoice` has range [0, 0] and AU validation reads back NaN from the
normalisation divide [R — ParameterIDs.h lines 113–117]. With a second instrument: `instrumentNames { "Rhodes",
"Wurlitzer" }`, **appended at the END of the layout** with `biasVolts` — hosts address by position;
`testParameterOrderIsStable` pins it. Choice index maps through a table `{ Instrument::rhodes, Instrument::wurlitzer
}`, never a cast: `enum Instrument` keeps its four-entry order (rhodes, wurlitzer, clavinet, cp70); whichever
instrument ships next extends the *table* (coordinate with `cp70-implementation-plan.md` §8.1 — names appear in ship
order). Switching = full `reset()` + rebuild; an audible gap is correct.

### 7.2 Two voice arrays, one active [D]

`std::array<WurliVoice, 88>` joins the tines (well under 0.5 MB total [C]); per-block dispatch on the instrument, as
the CP-70 plan lays out. The Wurlitzer path keeps the Rhodes' oversampled chain shape (sum at 4×, shared filters at
4×, one decimation) and swaps: field lookup → ΔC law, coil → shared RC highpass, SuitcasePreamp → WurliPreamp, panner
→ amplitude tremolo, Rhodes cab → Wurli speakers. Shared untouched: events, pedal state, ActionNoise (the 200A key
clack is real and travels through the case), Room, telemetry. Compass: authentic **A1–C7 (MIDI 33–96), 64 notes** [R];
the engine keeps its A0–C8 array and extrapolates outside, as both other instruments do [D].

### 7.3 No sympathetic coupling at launch [D]

The reed bar is a massive screwed-down steel bar whose clamp loss dominates the reed's decay [M] — a poor conductor of
note-to-note energy — and **no measurement of Wurlitzer sympathetic response exists on disk**. The CP-70 precedent
applies: absent evidence, coupling is an invented feature. Harp coupling forced 0; ActionNoise mixes dry. Open
question 7 names the cheap measurement.

### 7.4 Parameter mapping

| param | Rhodes | Wurlitzer |
| --- | --- | --- |
| velCurve, hammerMass, damperGrip, strikeNoise | as now | same roles |
| hammerHard | neoprene k factor | felt k factor on the Miessner-calibrated k |
| escapement | gap scale | let-off scale about 3.18 mm [R] |
| tipMass ("Tuning Spring") | spring position | **tongue thickness 0.020→0.026 in, µ re-solved** (§2.3) |
| resDamp | tine Q trim | **clamp-loss Q trim ×0.5–1.5** [M] |
| barCouple, barTune | tonebar | **hidden** — no tonebar |
| bodyMix | harp coupling | **hidden, coupling forced 0** (§7.3) |
| nonlinAmt ("Bloom") | stretch term | **hidden** — no measured geometric NL |
| pickupPos | voicing screw | **reed centring in the slot** — the manual's own voicing move [R]: offsets rest y, changing level and asymmetry |
| pickupDist | gap mm | **rest gap 0.3–0.8 mm**, default 0.5 (§5.6 floor) |
| coilFreq, coilQ, coilSat | magnetic voicing | **hidden** — no coil; nothing repurposed into a fake control |
| biasVolts (NEW, appended) | — | 130–170 V, default 150 (§5.6) |
| preampDrive | Suitcase | 200A preamp drive |
| bass, treble | Suitcase stack | convenience EQ, labelled (no tone controls on a 200A [R]) |
| tremRate/Depth | panner | amplitude tremolo, default 5.6 Hz / §6.3 depth |
| tremStereo | 1 = pan | **default 0**; still functional (stereo Wurli = labelled studio trick) |
| cabMix | Rhodes cab, 0.5 | Wurli speakers, 0.7 |
| spaceMix/Size, outGain, tune, phaser | shared | shared (tune re-solves µ, §2.2) |

Hiding is the UI's job; the DSP does not read hidden ones, and the "every control does something" test runs per
instrument on its visible set.

---

## 8. What WurliVoice does per sample

```
1. hammer.tick against patch-weighted reed displacement (felt HC, §4.1)
   -> force into modes 0,2,3 via sinc/dwell strike shape; beat partner
      (mode 1) gets the same force at its level offset
2. sys.tick()                     // pure linear, no SAV terms
3. return tip displacement        // one double
4. engine: Hermite tip path at 4x; per subsample
   y = knee((tip + centring)/gap);  bus += C0n * y/(1-y)
5. control rate: relative retirement checks, damper
```

Engine path per subsample: bus → shared one-pole HP (2312 Hz, gain u₀/C_total) → WurliPreamp (tremolo gain inside) →
speaker L/R → decimate → phaser/room as shipped.

---

## 9. Mode budget and CPU [C]

Budget rule unchanged: fs/π (15,279 Hz at 48 kHz) because the contact is explicit. Census (§1.3): ≤ 256 modes worst
case, 13% of the Rhodes pedal-down bank. Transduction: 64 voices × 4 subsamples × (one divide + one multiply-add +
knee branch) — cheaper than the Rhodes' four field lookups per voice, no per-voice tanh or coil. Estimate: **well
under half the Rhodes worst case**, dominated by the shared chain — still an estimate, so step 1 benchmarks a 64-voice
ff pedal-down chord (house rule). T60 up to ~54 s at A1 [M] means a pedalled bass chord runs a minute before retiring
— that is the instrument; relative thresholds and the 4-mode ceiling make it affordable without a shrink schedule.

---

## 10. Test plan

`tests/test_wurli_reference.cpp`, same row/verdict machinery as `test_epi_reference.cpp`. Reference chain for spectral
rows: tremolo off, `cabMix` 0, room off, bias 150 V, default drive. Targets from `wurlitzer-200a.md` unless tagged:

| # | Property | Target | Method |
| --- | --- | --- | --- |
| K1 | Harmonicity | partials 2–20 harmonic ±0.1 cent (sustain) | peak-fit vs k·f0 |
| K2 | Inharmonic floor | nothing > −45 dB between 3× and 25× f0 in sustain | pooled residual |
| K3 | Reed tuning | solved µ lands f0 ±0.5 cent across A1–C7, both thicknesses | unit-level solve check |
| K4 | Attack chirp | modes 2–3 at solved ratios, each < −40 dB, gone < 60 ms | STFT of first 100 ms |
| T1 | Decay anchors | dB/s at the 7 anchors ±30% | beat-suppressed envelope fit |
| T2 | Emergent per-partial decay | harmonic-k dB/s ratios monotonic, ±25% of {1.58, 2.60, 3.03, 3.83, 4.10, 5.27, 6.28}, k = 2..8 | per-harmonic envelopes, quiet note |
| T3 | Sustain control | resDamp scales T60 ×0.5–1.5, no frequency shift | envelope + peak fit |
| B1 | 2.4 Hz AM | sidebands around H1–H4, spacing 2.4 ± 0.5 Hz | sustain spectrum |
| V1 | Velocity bark, bass | A1 H2–12 vs H1: pp ≈ −4.9, ff ≈ +26.7 dB (±3); growth ≥ 28 dB | Goertzel bins, 4 velocities |
| V2 | Register law | E3 f +6.6, Db4 f −3.6, Db5 f −12.2 dB (±4), monotonic fall | same |
| P1 | Small-signal corner | 2312 Hz ± 1 dB vs first-order HP at tiny drive | swept sine through pickup path |
| P2 | Asymmetry | pos/neg peak ratio > 1.05 below corner (500 Hz, y ≈ 0.4) | direct (openwurli's test transplanted) |
| P3 | Superposition at the node | two-note render == sum of solo renders through pickup+HP (preamp off) to −80 dB | proves the shared-node factoring in code |
| P4 | Alias floor | folded residue < −70 dB, ff A1, full drive | Goertzel at folded bins |
| P5 | Treble bark absence | Db5 ff harmonic energy ≤ −10 dB (discriminates shared-node vs coupled-RC above corner) | V-row method at Db5 |
| A1 | Contact duration | 0.75–1.0 cycles of f0 at A1/C4/C6 (Miessner) ±30% | `contactSamples` |
| A2 | Bias parameter | output scales linearly 130→170 V (2.3 dB ± 0.2) | level measurement |
| M1 | Tremolo is amplitude | L/R gain correlation > +0.99 at tremStereo 0; rate 5.6 Hz ± 2% | telemetry |
| M2 | Tremolo depth | full depth ≈ 7.3 dB p-p (±1.5); depth falls at fast rates (opto lag) | gain trace |
| S1 | Preamp asymmetry | even harmonics rise with drive; clean below 1/3 drive | drive sweep spectrum |
| S2 | Speaker corners | ~95 Hz HP bump, ~5.5 kHz LP, ±20% | swept response |
| C1 | Adversarial CPU | < 0.5% missed deadlines, 64-note ff pedal glissando at 48 kHz | stress harness |
| C2 | Retirement | ff A1 under pedal retires; > 60 s allowed | energy trace |
| N1 | No NaN / energy growth | energy never rises in any linear segment | S-row machinery |

Row V1 is the calibration row for the y-scale (gap + hammer energetics) — expected to drive the calibration pass, and
the plan says so now.

---

## 11. Deliberately not in this plan

Per-partial damping (measured emergent, forbidden as a parameter [M]); a modal bank at harmonic ratios (double-counts
the pickup [M]); pickup back-action (computed negligible over the exposed gap range [C]); a second polarisation
(§1.2); reed-bar sympathetic coupling (no data, §7.3); a tube stage (the 200A has none [R]); Miessner's toroidal
tuner-damper (patent-only, never in production 200/200A units [R]); openwurli's β₂ table (wrong [M]) and its MLP
correction layer (a sample-matching device — the opposite of this project's discipline).

---

## 12. Open questions, ranked by risk

| # | Unknown | Risk | Closes with |
| --- | --- | --- | --- |
| 1 | C_total (240 pF single-sourced) → corner ±30%, the register balance | High — the instrument's main bass/treble mechanism | One LCR reading of a real reed bar |
| 2 | Clamp offset + per-note µ (§2.2 pair) | Medium-high — owns attack chirp and strike mass | Attack STFTs of real notes (f2/f1 per register pins µ); one photo of a bare reed bar pins the clamp line |
| 3 | Rest gap (undocumented; 0.5 mm assumed) and per-note y-scale | Medium-high — owns row V1 | The V1 calibration; better, a feeler gauge on a real comb |
| 4 | 2.4 Hz AM mechanism | Medium — currently phenomenological | Sideband spacing at H1 vs H4 in real recordings: k·δ ⇒ two mechanical components; constant δ ⇒ global gain modulation |
| 5 | Shared-node vs coupled-RC above the corner | Medium — model structure | Row P5 + peak-asymmetry vs frequency on real treble notes |
| 6 | Hammer mass and strike point | Medium — attack character | A scale and a ruler on one action; until then rows A1/K4 calibrate |
| 7 | Sympathetic response with pedal down | Low-medium | One recording: pedal down, one ff bass note, listen to the residual after hand-damping it |
| 8 | Undamped top range? | Low | One recording of a released C7 |
| 9 | Tremolo-in-feedback breathing (gain/BW/THD together) | Low | A/B against any 200A recording with vibrato up |
| 10 | LFO shape (twin-T distortion residual) | Low | Same A/B; openwurli's circuit trace as reference |

---

## 13. Implementation order

1. **Pickup unit first** (the timbre owner): ΔC law + knee + shared HP standalone; rows P1–P5, V1–V2 driven by a
   synthetic decaying sine. Validates the shared-node factoring before any voice exists.
2. Reed solve: ruler-law geometry, clamp offset, per-note µ bisection, anchored Q. Rows K1–K3, T1, T3.
3. `WurliVoice`: felt hammer + patch + beat partner + dampers. Rows K4, A1, B1, T2.
4. Engine dispatch, selector + biasVolts at end of layout, WurliPreamp, tremolo shape blend, speaker corners. Rows
   M1–M2, S1–S2, A2, C1–C2, N1.
5. Calibration pass against real 200A recordings: y-scale/gap (V1), strike point + hammer k (K4/A1 jointly — calibrate
   coupled parameters together, the Rhodes striking-line lesson), per-note level trims (C₀ₙ graduation).
6. Presets ("200A DI", "200A speakers + vibrato"), UI panel, checklist doc.

Sources: `docs/research/wurlitzer-200a.md` and `docs/research/transducers-and-chassis.md` §3/§5.1 (carrying the
primary citations: Series 200/200A service manual, 200A schematic #203720-S-3 via openwurli, DAFx-17 §4.3/5.5/5.6,
DAGA 2017, docwurly reed tables, Miessner US 2,932,231 / 3,038,363 / 3,215,765), plus the measured Rhodes architecture
in `src/epi/dsp/` this reuses.
