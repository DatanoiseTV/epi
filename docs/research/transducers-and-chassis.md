# Transducers and chassis — reference

Working notes for widening the Epi model beyond the one implemented
transducer. Companions: `rhodes.md` (electronics), `wurlitzer-200a.md`
(reed + electrostatic pickup measurements), `cp70-measured.md` (piezo chain),
`acoustic-checklist.md` (the numbers the model must hit). Everything here is
quoted from a document on disk, computed from quoted numbers and marked so,
or explicitly marked **not found**. Nothing below is a guess dressed as a fact.

The architectural premise, verified three ways across two instruments
(ISMA 2014 §3; 200A spectra measured to ±0.1 cent; DAFx-17 §5.5): **the
resonator delivers one sine per note and the transducer manufactures the
timbre.** Every section below is therefore about transducers first, and about
the metal and wood only where a signal path into the output actually exists.

---

## 1. Pickup geometry families for the magnetic transducer

Four families are physically documented. All four fit one framework: a static
field map B(v, g) sampled by the tip position, differentiated, then filtered
by the coil circuit (§2). They differ only in the shape of B — which is
exactly what `MagneticPickup::Geometry` + the prepare()-time integrator
parameterise.

| Family | Field model | Symmetry about rest | Harmonic signature | Source |
| --- | --- | --- | --- | --- |
| Rhodes wedge/chisel | Coulombian line charge over a receding face, DAFx-17 Eq. 7 | narrow bell, near-cusp at the flat | centred: even-dominant (double crossing); offset: fundamental returns; off-the-end collapse: growl | DAFx-17 §5.4; DAGA 2017; US 2,972,922 |
| Cylindrical pole (guitar) | disk of magnetic charge, radial integral | wide bell (pole radius ≫ swing) | mostly linear; gap modulation adds odd+even, emphasis to H3; across-pole motion frequency-doubles but is 14–18 dB down | Horton & Moore 2009; Paiva et al. 2012 §6 |
| Bar / blade (Clavinet) | long rectangular face, flux ~constant along the bar | flat along the bar; exponential in the gap | along-bar motion negligible (−25 to −30 dB); all distortion from gap coordinate; smooth asymmetric, no even-harmonic doubling mechanism | Remaggi et al., DAFx-12 |
| Sphere-over-coil (analytic) | magnetised sphere near a single-turn loop, closed form | one-sided in gap | asymmetric odd+even from the closed-form flux; no lateral axis at all | Falaize & Hélie, JSV 390 (2017) |

### 1.1 The wedge, and what the sources actually establish

- The chisel edge is **original 1961 equipment, not a later refinement**:
  US 2,972,922 already discloses "chisel edge 46 ... perpendicular to the
  common plane" of tine and tonebar, adjusted "somewhat off-center relative
  to the axis of tine 22" for "accurate adjustment of the fundamental-overtone
  relationships". There is no pre-wedge magnetic Rhodes pickup to model.
  What predates the magnetic pickup is electrostatic — see §3.4.
- DAFx-17 Eq. 7 is a **three-part** integral: receding slant, flat tip,
  receding slant — precisely `surfaceGap()`'s piecewise profile. The tine
  trajectory in Eq. 8 is a circular arc about the clamp,
  z′ = r − sqrt(r² − x′²), which is why the gap axis of the table is dense
  (the arc modulates g at 2×f0 every cycle).
- DAGA 2017 (FEM in COMSOL): "the flattened sides of the frustum focuses the
  magnet field in the center showing an approximate bell curve characteristic"
  and the flux disturbance "gets more and more asymmetrical" with deflection.
  Independent confirmation of the shape the table already encodes.

### 1.2 Cylindrical pole — the counterfactual "guitar pickup on a tine"

Horton & Moore model the pole as two disks of uniform magnetic charge
(±σ, separated by magnet length) and the string as a chain of induced
infinitesimal magnets; validated against Hall-probe measurements to within
plot resolution, single fitting parameter. Their integral (their Eq. 3):

```
Bz(x′,y′,z′) = ∫₀^2π ∫₀^ψ  σ (z′−z) ρ / [ (x′−x+ρcosφ)² + (y′−y+ρsinφ)² + (z′−z)² ]^{3/2}  dρ dφ
```

What it predicts for a tine swinging across a round pole:

- **Vertical (gap) motion dominates**: their calculated horizontal-motion
  signal had to be magnified 5× (single magnet) to 8× (bass pickup) to be
  visible — 14–18 dB down. Paiva's Vizimag FEM agrees: flux vs gap is a
  near-exponential decay, flux vs lateral position a bell.
- **Across-pole motion frequency-doubles** (symmetric bell, two maxima per
  cycle) — same mechanism as the centred wedge, but the bell is the pole
  radius (~3–6 mm) wide, not 0.28 mm, so a normal tine swing explores a
  fraction of its curvature. Knob-visible result: cleaner, rounder, far less
  velocity-dependent brightening; the "growl" gap-collapse regime is out of
  reach until the gap control is nearly closed.
- Distortion never vanishes: gap-coordinate asymmetry generates odd+even with
  H3 emphasis (Paiva Fig. 18) even for pure vertical motion.

### 1.3 Bar / blade — the Clavinet pickup, measured

DAFx-12 (Remaggi, Gabrielli, Paiva, Välimäki, Squartini), physical inspection
of a real instrument: six epoxy-potted bar coils per pickup, each bar 0.5 cm
wide × 3.3 cm (bridge) / 3.7 cm (centre) long, ten strings per bar. Vizimag
sweeps 0–20 mm in 21 steps:

- Flux vs **vertical** displacement: negative exponential (their Fig. 5b),
  fitted by a 4th-order polynomial (p0..p4 published) or an exponential.
- Flux vs **along-bar** displacement: nearly flat, maximum at bar centre;
  energy ratio E_vert/E_horiz = 25 dB (centre string) to 30 dB (edge string)
  for 1 mm p-p motion. They do not model the lateral axis at all.
- The linear signature is the **position comb** H(z) = 1 − βz^(−2N): centre
  pickup 18.5→6.5 cm from termination (4.2 ms at 161 Hz), bridge pickup a
  constant 4 cm (1.8 ms), tilted ~30°.

For a blade under a Rhodes tine (long axis along the swing): the swing sees a
nearly constant field, so the output is almost pure fundamental plus the weak
arc-driven gap asymmetry — the "darkest" possible member of the family, the
opposite pole from the wedge.

### 1.4 Analytic sphere model (Falaize)

Falaize & Hélie treat the tine tip as a magnetised sphere of the beam radius
over a single equivalent turn: flux φp in closed form (their Eqs. 22–27,
with fφ(q) = (q+Rp)/sqrt(L_hor² + (q+Rp)²)), then a gyrator into an RC.
Useful as a cross-check family with zero table cost. **Caution carried over
from `rhodes.md`: their published Eq. (22) is dimensionally wrong; use (25).**
Their pickup is analytic, not fitted — it has no wedge, so it cannot produce
the centred even-harmonic signature; treat it as a validation reference for
the gap axis only.

### 1.5 Hosting all four in the current code

`MagneticPickup` needs no architectural change. The audio-rate path (257×129
table, Catmull-Rom in v, linear in g, clamp at 8 half-widths where the field
is three orders down) is shape-agnostic. Per family, at prepare() time only:

| Shape | `surfaceGap()` | `integrate()` |
| --- | --- | --- |
| Wedge (shipped) | flat + linear recession | 1D line, 192 midpoint steps — keep |
| Bar/blade | flat over ±halfWidth, wedgeDepth = 0 | unchanged (wider face) |
| Cylindrical | n/a — face is a disk | 2D disk quadrature of Horton & Moore Eq. 3, ~192×48 samples; still milliseconds at prepare() |
| Sphere (Falaize) | n/a | skip the table: closed form Eqs. (23)/(25) evaluated into the same grid |

A `PoleShape` enum inside `Geometry` plus a switch in prepare() is the whole
feature. The existing controls keep their meaning: `pickupPos` slides across
the bell of whatever shape is active, `pickupDist` moves along its decay.
Verification per shape: render the harmonic-vs-offset map (H1, H2, H3 against
offset at fixed swing) and assert the family signature — even-dominant at
centre for wedge, frequency-doubled residual −14 dB or lower for cylinder,
offset-independent spectrum for the blade; alias floor unchanged from the
shipped wedge (the reference suite's inharmonicity rows already gate this).

---

## 2. The load circuit is part of the transducer

### 2.1 Measured values for real pickups (guitar, the only published set)

Paiva et al. 2012 §5 (drawing on Jungmann's 1994 HUT thesis measurements):

| Element | Range | Effect |
| --- | --- | --- |
| L | 1 – 10 H | sets resonance f_R = 1/(2π√(LC)); larger L → lower, hotter peak |
| R (DCR) | 5 – 15 kΩ | mild; damps the peak |
| C (winding) | 15 – 200 pF | sets resonance with L; cable adds directly to it |
| R_l (eddy/core loss) | 300 kΩ – 3 MΩ | peak amplitude; a low parallel load (volume pot) both attenuates and de-Qs |

Transfer function (their Eq. 30): a driven RLC divider,
H = 1 / (1 + R/R_l + s(L/R_l + RC) + s²LC) — second-order lowpass with a
resonant peak, DC gain R_l/(R+R_l). Tone capacitor (~1 nF) drags the
resonance down an octave-plus and kills the top. This is exactly the shape
`PickupCoil`'s SVF already has; what it lacks is the *derivation of f and Q
from circuit values*, so cable and load could become honest controls.

### 2.2 What is actually known about the Rhodes coil

From `rhodes.md`, re-stated because every candidate feature depends on it:

| Parameter | Value | Source |
| --- | --- | --- |
| DCR, one pickup | ~180 Ω (170–190) | Vintage Vibe |
| Wire | AWG 37 | ep-forum, measured |
| Magnet | Alnico 5, 0.5 × 0.1875 in | US 4,040,321 |
| Turns, L, C, resonant peak | **NOT FOUND — confirmed gap, do not invent** | forum thread asking exactly this, no answer |
| Harp wiring, 73-key, ≥ early '70s | 24 series groups (one of 4, twenty-three of 3 parallel) | service manual + rhodes.md |
| Total harp DCR | ~1.4–1.6 kΩ (calc. 1425, measured in range) | rhodes.md |

### 2.3 What the wiring does and does not change — derived, load-bearing

For n_s series groups of n_p identical coils: L_tot = (n_s/n_p)·L₁,
C_tot = (n_p/n_s)·C₁, R_tot = (n_s/n_p)·R₁. Two consequences:

1. **The intrinsic resonance is wiring-invariant.** ω = 1/√(L_tot C_tot)
   = 1/√(L₁C₁) for any series/parallel arrangement of identical coils —
   Paiva's Eq. (54) result. A single lumped resonance is therefore not an
   approximation to the 70-coil chain; for identical coils it is **exact up
   to gain and impedance**. The current one-SVF `PickupCoil` is the right
   topology, not a shortcut.
2. **The external capacitance is not invariant.** The 1970s rewiring
   (12 groups of 6 → 24 groups of 3) multiplies L_tot by 4 while a guitar
   cable's ~500 pF–1 nF does not move. Intrinsic C_tot is C₁/8 — tens of pF
   at most — so the cable dominates the total C and the observable resonance
   is f ≈ 1/(2π√(L_tot·C_cable)): **the rewiring halved the loaded resonant
   frequency** at the same time as it doubled output voltage (+6 dB) and
   quadrupled source impedance. Derived, not measured — but every input is a
   quoted number except L₁, which cancels out of the *ratio*.

### 2.4 What the lumped model misses — itemised with a verdict each

- **Mutual inductance / crosstalk between neighbouring pickups: negligible,
  for two stacked reasons.** Electrically, all 73 coils already sum into one
  output, so coupling redistributes nothing a listener can reach. Magnetically,
  the Pfeifle FEM (DAFx-17 §5.4, citing their Fig. 8 of [18]) shows only a
  small region of the tip carries field; at ~13 mm pole spacing a neighbouring
  tine sits far outside it. No modelling action.
- **Distributed capacitance of the chain: irrelevant** below the (ultrasonic)
  self-resonance of a single 180 Ω coil; the ladder of identical sections
  collapses to the lumped equivalent (§2.3 point 1).
- **Cable + following-stage load: real and missing.** This is where the SVF's
  abstract (f, Q) should come from: f from L_tot·(C_cable + C_input), Q from
  R_tot, R_l and the load. The loads are documented: Janus input 0.1 µF into
  10 kΩ → 127 Hz highpass with the harp as source; volume pot 5 kΩ audio
  loaded by a tone stack whose input impedance runs 23.0 kΩ at 50 Hz to
  3.96 kΩ at 10 kHz — explicitly *not* a clean log divider; Stage output is
  the 50 kΩ reverse-audio rheostat ‖ 47 nF shelf (flat → −15.6 dB bass) into
  a 10 kΩ pot. All in `rhodes.md` with the solved dB tables.
- **Eddy losses (R_l): currently folded into Q.** Fine until the load becomes
  a control; then R_l is the element that makes "volume knob down = duller"
  come out right (Paiva Fig. 13b: a 1–10 kΩ parallel load both attenuates and
  flattens the peak).

The honest calibration order, since L₁ is unpublished: pick L_tot to place
the loaded resonance where the reference recordings say the top end sits
(G-section centroid rows), then let cable/load controls move it by circuit
law rather than by ear. One measurement of a real pickup with an LCR meter
would replace the one free parameter — worth soliciting.

---

## 3. The electrostatic pickup — implementation dossier

Everything needed to build the Wurlitzer transducer is on disk; this section
consolidates it so the build needs no re-research.

### 3.1 The law

Reed as the grounded electrode of a charged capacitor (DAFx-17 §5.6, Series
200 manual): with bias u₀ and the two assumptions Pfeifle states (linear
charge curve in range, constant supply during a cycle),

```
i(t) = u₀ · dC/dt,        C(y) = C₀ / (1 − y),   y = displacement / rest gap
```

the current develops a voltage across the feed/load network → the preamp.
The 1/(1−y) comes from the parallel-plate limit; DAFx-17 computes C per
slice by FEM (Gauss's theorem over the slotted-comb geometry) and confirms
the shape. The reed vibrates **through symmetric cutouts in the plate**
("the reeds are designed to vibrate freely between the symmetric cutouts",
DAFx-17 §5.6; the 200A manual's "off center **in** the pickup" language and
the "reed shorted against pickup" fault entry corroborate the slotted-comb
reading — see `wurlitzer-200a.md`). The direction asymmetry survives anyway
because the reed itself is not symmetric (DAGA 2017, Fig. 9: "the capacity
change differs at each excursion depending on moving direction").

### 3.2 The numbers

| Parameter | Value | Source |
| --- | --- | --- |
| Bias, 200A | **150 V nominal**, real units sag (130 V measured by Pfeifle) | 200A assembly drawing; DAFx-17 |
| Bias, Series 200 | 160–170 V — a different instrument, do not cross-apply | Series 200 manual p.4 |
| C₀ rest capacitance | 240 pF | openwurli |
| R_total | 287 kΩ (openwurli: 1 M ‖ 402 k) **or** 234 kΩ (manual R-56 = 560 k ‖ 402 k) | openwurli; Series 200 manual |
| → highpass corner | 2312 Hz vs **2834 Hz — a 23% discrepancy landing on the bass/treble balance**; verify against 200A schematic 203720-S-3 before freezing | computed, `wurlitzer-200a.md` |
| Sensitivity | u₀·C₀/(C₀+C_p) = 147·3/240 → 1.8375 V scale on the AC perturbation | openwurli |
| Back-action | softening −0.048 N/m vs 539 N/m mechanical → −0.077 cents; load power 0.03% of stored energy. **Pure sensor, no detuning** — but softening ∝ 1/d³, ~1.2 cents at 0.2 mm | computed, `wurlitzer-200a.md` |

### 3.3 The discretisation that works (openwurli `pickup.rs`, reviewed)

Do not do "static y/(1−y) then a separate HPF". Discretise the actual RC with
the time-varying capacitance so nonlinearity and filtering stay coupled:

```
R_total · C(y) · dV/dt + V = V_hv      — normalised charge q, equilibrium q = 1
β = dt/(2τ), τ = R_total·C₀;  per sample:  α = β(1−y)
q ← (q(1−α) + 2β)/(1+α);   out = (q(1−y) − 1) · sensitivity
```

This buys three physically correct behaviours the split model cannot give:
identical small-signal HPF at the RC corner; H2 generation that is **strong
below the corner and fades above it** (the charge cannot follow fast
capacitance changes); and the correct asymmetry sign (motion toward the plate
amplified more). Guard the y → 1 singularity with a C¹ soft knee, not a hard
clamp — openwurli's hard clamp at 0.98 measurably sprayed broadband hash and
was replaced by identity below 0.94 + tanh knee to 0.98. Drive it with
**displacement relative to the gap** and the 32 dB pp→ff bass harmonic
growth and the treble that never barks both emerge — do not hand-tune per key
(`wurlitzer-200a.md`, velocity table).

Tests, all against the measured tables in `wurlitzer-200a.md`: partials
harmonic to ±0.1 cent to H20; harmonic k decays k× the fundamental's dB/s
(measured ratios 1.58–6.28 for k=2..8); H2–12 energy vs velocity (A1:
−4.9 → +26.7 dB pp→ff); small-signal corner within 1 dB of the chosen R·C₀;
positive/negative peak asymmetry present below the corner, absent above.

### 3.4 A historical footnote that licenses a hybrid

The 1946 Rhodes Pre-Piano shipped with an electrostatic pickup ("an
electrostatic microphone, amplifier and 6″ speaker", fenderrhodes.com
history) — the Rhodes lineage *started* electrostatic. An
electrostatic-on-tine option is therefore not a fantasy pairing but the
instrument Harold Rhodes actually built first; it needs only the §3.1 law
fed by tine displacement and a gap/bias pair of controls.

---

## 4. Piezo (CP-70) — cross-reference only

The CP-70 chain is measured and documented in `cp70-measured.md` (88 elements
on a segmented bus, 0.022 µF mixing capacitors, C101 0.1 µF into 470 kΩ,
charge ∝ force at the termination); the implementation plan is owned by
`cp70-implementation-plan.md`. What Zollner ch. 6 adds that transfers and is
not in either document:

- A bridge piezo is a **force→voltage converter, flat to first order**:
  measured 0.2 V/N on an Ovation EA-68, frequency-independent once the
  1.4 g measurement-head mass artefact (3.9 kHz resonance with the 800 kN/m
  bridge stiffness) was compensated. Structural resonances of the mount ride
  on top — the u-rail added a 5 kHz dip when badly seated. Lesson for the
  model: the piezo itself contributes no curve; any colour belongs to the
  termination impedance and the electrical load.
- The source is **capacitive** (0.45–1.5 nF measured on two Ovations), so the
  load resistor sets a first-order highpass — 1 MΩ → 106 Hz, 500 kΩ → 220 Hz,
  2 MΩ → 177 Hz, 50 kΩ line input → 2.1 kHz ("complete loss of the lows") —
  and a long cable is a **broadband capacitive divider, not a treble cut**.
  The CP-70's own 470 kΩ bias resistor sits in exactly this role.
- Zollner validates the whole two-port by reciprocity (sensor vs actor
  transfer matched via TxU = C·TUF) — the same symmetric-transduction
  discipline as the project's sense-and-force rule in contact modelling.

---

## 5. Resonator variants

The framework (`CantileverModes` → per-note mode set → `SavModalSystem`)
carries all of these; what changes per variant is the characteristic
equation, the shape values at strike and pickup points, and the damping law.

### 5.1 Wurlitzer reed — non-uniform cantilever with a tuned tip mass

- Geometry, all verified in `wurlitzer-200a.md`: lengths 2 19/20 in → 1 in by
  an exact ruler law; widths 0.151 → 0.096 in; tongue 0.020 in early /
  0.026 in late; Sandvik steel; solder blob tunes by mass.
- Tip-mass ratio µ ≈ 1.7 (bass) → 0.1 (treble), computed from geometry.
  Tip mass pushes partials *away* from harmonic (f2/f1 from 6.27 toward
  8–14): **no µ makes a reed harmonic**, which is why the architecture stays
  one-mode-plus-nonlinear-pickup. Solve the tip-mass characteristic equation
  fresh — `openwurli`'s β₂ table is wrong by 18% at µ=0.5 and does not
  converge to the clamped-pinned limit; do not inherit it.
- Damping: Q ≈ 1000 flat across the compass (median 970), hysteretic; clamp
  loss dominates and is the physically correct sustain control. Per-partial
  decay must **emerge** from the pickup nonlinearity (measured k-ratios in
  §3.3), never be parameterised.
- The 2.4 Hz two-polarisation beat is much of why a static sine sounds dead;
  same-frequency/different-decay treatment as the Rhodes B5 plan.

### 5.2 Early Rhodes tine designs — service manual ch. 7, primary source

Three generations, all documented: (1) plain piano wire, 0.075 in
(1.905 mm) diameter, tuned by a crimped slideable spring — the tuning spring
survives to the end; (2) centerless-ground **taper**; (3) swaged taper
(current). Durability: 40k / 1.5M / 6M+ heavy blows respectively. For the
model: generation 1 is a *uniform* cylinder — textbook mode ratios 6.27,
17.55, 34.39 and a different strike-point shape from the shipped tapered
solve; generation differences change the attack's inharmonic content (C-rows)
and the mode ratios, not the steady state (which the pickup owns). Cheap
variant with a real historical referent ("Raymac"-era voicing); the checklist
suite re-run under generation-1 geometry is the whole verification.

Also from the manual and already flagged in `acoustic-checklist.md` known
gaps: the striking line runs 2¼ in (57.15 mm) at the extreme bass to ⅛ in
(3.175 mm) at the extreme treble — any resonator-variant work must not
re-calibrate against the currently-wrong strike line without taking that
whole gap on.

### 5.3 Clavinet string — the one resonator that is NOT a sine

A struck-tangent string carries a full harmonic series, so here the resonator
*does* make the timbre and the pickup adds colour: position comb
(1 − βz^(−2N), delays 4.2 / 1.8 ms for centre/bridge on the 67.8 cm string),
the bar-pickup flux exponential of §1.3, and four switch states (A/B × C/D:
centre, bridge, sum, difference — anti-phase damps the fundamental; the enum
and parameter already exist in `EpiEngine.h`/`ParameterIDs.h`). The string
model is a genuinely new class of resonator for this codebase (dispersive
string + tangent contact + yarn damper at one end); Gabrielli et al. 2013
(EURASIP) is the methodological template and is on disk. The pickup aperture
lowpass (Paiva §3: sensitivity width as an FIR over the sensed segment)
matters here and for the CP-70, and is meaningless for the Rhodes point-tip.

### 5.4 Pianet — **insufficient data on disk**

Mechanism (uncontested): leather/adhesive pad grips the reed, key release
peels it off — a pluck with a force profile set by adhesion; reed then rings
over a pickup (electrostatic in the N, electromagnetic in the T family).
Nothing quantitative on disk: no reed dimensions, no pad force law, no pickup
geometry, no measured spectra. Do not build until sourced; the reed side will
reuse §5.1 machinery, so the research need is the *pad adhesion law* and the
T-model pickup geometry. Marked as the only variant with a research
prerequisite rather than an implementation one.

---

## 6. Chassis and enclosure

### 6.1 The harp frame — what is measured and what is invented

What ISMA 2014 actually measured (Table 1, impulse hammer + piezo) is the
**tonebar**, not the frame: lowest tonebar eigenfrequencies 51–222 Hz across
the compass, non-monotonic (140 Hz at bar 54, 145 at 61, 222 at 68), always
far below the sounding note, enslaved to the tine's frequency in phase or
anti-phase. That data is already consumed by the D-section of the checklist.

**No measured modal data for the harp frame / tone bar rail assembly exists
in any document on disk, and none was found to fetch.** The six modes in
`Harp.h` ({47, 88, 143, 211, 305, 418 Hz}, T60 0.42–0.11 s, 4 kg) are
invented and stay flagged as such. Three honest observations about them:

- They are *bounded*, not free: E1 (0.02–0.6 dB slow AM on H1) caps how much
  a frame mode may modulate a held note, and the pedal-down chord bloom is
  the audible product the invention was tuned to. Any replacement must pass
  the same rows.
- The invented band (47–418 Hz) brackets the measured *tonebar* band
  (51–222 Hz); the top two modes have no measured counterpart of any kind.
- The measurement that would settle it is cheap for an owner: one accelerometer
  impulse response on the tone bar rail with dampers down, FFT, six peaks and
  their T60s. Worth soliciting alongside the LCR reading of §2.4; no sample
  library can separate frame from tine after the fact.

### 6.2 The Suitcase cabinet

Fact base: the service manual's own schematic titles establish a **dual
50 W power amplifier** (two channels) for the 100 W Suitcase/Janus I, and an
80 W Peterson unit; the vibrato pans between the two channels (topology
solved in `rhodes.md` — trapezoid LFO, Vactrol asymmetry, no pan law). The
speaker complement is commonly documented as **four 12-inch drivers, two per
channel — not verified against a factory document this session**; the manual
text on disk names no speaker sizes and two targeted fetches came back empty.
The `Cabinet` comment in `OutputChain.h` asserts 4×12 and should carry the
same caveat until a parts list confirms it.

Physically-derived vs the shipped bandpass+bump+tanh: a closed box is a
2nd-order highpass at the system resonance, the cone a lowpass with a breakup
bump, suspension travel a saturator — i.e. the shipped model already has the
physically correct *shape* (75 Hz / 4.2 kHz / 1.8 kHz presence / tanh). A
genuine upgrade needs the driver identified (Thiele–Small parameters) or a
measured IR of a real Suitcase cab, neither of which exists on disk. Given
that the plugin's primary output is the DI/preamp path and the cabinet is a
mix-in colour, this ranks last in §7 on payoff — the honest move is the
caveat, not a speculative "physical" cabinet with invented T-S numbers.

### 6.3 Does the wooden case matter for a DI'd signal?

No — by the same argument that anchors the whole model. The pickup senses
one relative coordinate: tine tip vs pole piece. Both are mounted on the
harp; a case or key-bed vibration that moves the harp moves tine clamp and
pickup rail **together**, and reaches the sensed coordinate only by flexing
the tine through its clamp — which is precisely the spring-coupling path
`Harp.h` already implements (equal-and-opposite, passive). Airborne coupling
to a 1–2 mm steel tip is negligible at any realistic SPL. The case's real
outputs are acoustic (irrelevant DI'd) and mechanical thump into the key bed
(`ActionNoise`, modeled). Conclusion: no case model for the DI path; the
case matters only if a mic'd-Stage ambience is ever added, and then as an
acoustic radiator, not as a signal-path filter. This is the
"magnetic-pickup-cannot-hear-wood" principle made specific: it cannot hear
anything that moves source and sensor in common mode.

---

## 7. Build plan — ranked by (evidence × payoff) / cost

| # | Feature | Evidence | Payoff | Cost | Verification that gates it |
| --- | --- | --- | --- | --- | --- |
| 1 | **Wurlitzer voice**: one-mode reed (measured T60/Q, polarisation beat) + §3 electrostatic pickup | strong — measured decay/velocity tables, working reference impl, DAFx-17 | a second instrument, enum already waiting | medium — reuses SavModalSystem, hammer, harp analogue (reed bar) | §3.3 test list; ±0.1 cent harmonicity; k× decay ratios; A1 pp→ff +32 dB |
| 2 | **Circuit-derived PickupCoil**: f, Q from L_tot, C_cable+C_in, R, R_l; cable + load as controls; Stage 50k‖47n shelf | medium-strong — every element but L₁ documented; L₁ calibrated once against G-rows | the "one pickup sounds unlike another" half the current abstract f/Q can't reach; volume-knob de-Q for free | low — same SVF, new coefficient derivation | transfer function matches Paiva Eq. (31) for the set values; rewiring A/B reproduces +6 dB and the halved loaded resonance of §2.3 |
| 3 | **Pole-shape family** in `MagneticPickup` (blade, cylinder, sphere) | strong physics per shape; counterfactual instruments by design (see project memory: transduction laws, never waveshaper presets) | the highest-leverage timbre axis this architecture owns | low-medium — prepare()-time integrand swap, §1.5 table | per-shape harmonic-vs-offset signature; alias floor and reference suite unchanged for the shipped wedge |
| 4 | **Electrostatic-on-tine hybrid** (Pre-Piano lineage, §3.4) | medium — law measured on the Wurlitzer, pairing historical | novel-but-honest voice; cheap once #1 exists | low after #1 | small-signal corner + asymmetry tests from §3.3 driven by tine motion |
| 5 | **Clavinet voice**: string + tangent + comb + bar pickup + switch matrix | strong — DAFx-12/EURASIP numbers on disk | third instrument; only one with a true harmonic resonator | high — new resonator class | comb notches at the measured 4.2/1.8 ms delays; E_v/E_h ≥ 25 dB; anti-phase sum damps H1 |
| 6 | **Early-tine variant** (1.905 mm uniform wire, gen-1) | strong provenance (SM ch. 7), weak audio reference | period voicing; attack-content variety | low — alternate geometry into the existing solve | checklist C-rows re-run; steady-state rows must not move |
| 7 | **Harp frame from measurement** | none — data does not exist (§6.1) | unknown until measured | blocked on a measurement, not on code | E1 and pedal-bloom rows on the replacement |
| 8 | **Physical cabinet** | weak — no driver ID, no IR | low for a DI instrument | medium | n/a — do not build on invented T-S parameters |

CP-70 items are deliberately absent: owned by `cp70-implementation-plan.md`.

Two measurements worth soliciting from any Rhodes owner before items 2 and 7
harden: one LCR-meter reading of a single pickup (kills the only free
parameter in #2), one accelerometer tap test of the harp assembly (unblocks
#7). Both are minutes of work with the harp cover off.

---

## Sources

On disk (scratchpad unless noted):
- Pfeifle, DAFx-17 — `dafx17_rhodes.txt` (§5.4 Rhodes pickup Eq. 7–8, §5.5 reed, §5.6 electrostatic)
- Muenster & Pfeifle, ISMA 2014 — `isma.txt` (tine sine, tonebar Table 1, voicing)
- Pfeifle & Münster, DAGA 2017 — `daga2017_rhodes.txt` (FEM frustum/bell, capacitance asymmetry)
- Paiva, Pakarinen & Välimäki, JAES 60(10) 2012 — `pickups.txt` (circuit values, aperture, series/parallel, FEM flux)
- Horton & Moore, Am. J. Phys. 77(2) 2009 — `horton_moore.pdf/.txt` (disk-charge model, vertical vs horizontal)
- Remaggi, Gabrielli, Paiva, Välimäki, Squartini, DAFx-12 — `epi-research/dafx12_clav_pickup.txt` (bar pickup measurements)
- Gabrielli et al., EURASIP JASP 2013 — `epi-research/gabrielli2013.txt` (Clavinet template)
- Falaize & Hélie, JSV 390 (2017) — `falaize_jsv.txt` (analytic sphere pickup, port-Hamiltonian; Eq. 22 dimensionally wrong, use 25)
- Zollner, *Physik der Elektrogitarre* ch. 6 (piezo) — `zollner6.txt`. **Note: this
  file is the piezo chapter; the magnetic-pickup chapter (5) with Zollner's own
  R/L/C/eddy measurements is not on disk** — Paiva/Jungmann stand in for it above.
- `openwurli` `pickup.rs` — time-varying-RC discretisation (β₂ mode table elsewhere in that repo is wrong; see §5.1)
- Rhodes Service Manual — `rhodes_sm.txt` (ch. 7 tine generations, ch. 10 wiring + "approximately 2500 ohms", striking line)
- Wurlitzer Series 200/200A Service Manual — via `wurlitzer-200a.md` (R-56, bias rails, voicing language)

Fetched this session:
- US 2,972,922 (Google Patents) — chisel-edge pickup disclosed in 1961, off-centre voicing
- fenderrhodes.com history — Pre-Piano electrostatic pickup, $99.50
- fenderrhodes.com manual ch. 11 index / Wikipedia — Suitcase dual-channel confirmed; speaker complement **not** confirmed

Known missing after active search: Rhodes coil L and C (§2.2); harp frame
modal data (§6.1); Suitcase speaker complement from a factory document
(§6.2); any quantitative Pianet source (§5.4); Zollner ch. 5; Pfeifle &
Muenster Springer 2017 chapter (closed access — likely holds the Wurlitzer
pickup gap and reed FEM shapes).


---

## Addendum: primary measurements landed (2026-08-17)

Four sources supplied by the project owner, fetched and mined. The top-ranked
open question -- the Rhodes pickup's electrical values -- is now half closed.

### Measured values

| Quantity | Value | Source |
| --- | --- | --- |
| Whole-harp inductance, 1973 Stage 88 | **854 mH** | Morrin, measured on his own unit |
| Whole-harp DC resistance, same unit | **2.1 kohm** | Morrin |
| Single pickup DCR (approximate) | ~180 ohm | Sean, ep-forum, stated as approximation |
| 73-key harp, groups-of-three | ~1425 ohm calculated | Sean, consistent with the above |
| Output impedance after the 70s rewiring | ~2500 ohm | Service manual ch. 10 |
| Early Stage tone network | 10k audio pot + 1 uF, 172 Hz LP | Morrin, calculated from his schematic trace |
| Later Stage bass-boost network | 47 nF, 338 Hz HP; 5k parallel | Morrin |
| Treble section coupling | last 13 pickups behind a 4.7 nF series cap | Morrin |
| Humbucking | adjacent pickups wired in opposite polarity | Sean |
| Groups-of-nine experiment | "significantly quieter than groups-of-three" | Sean, measured by rewiring |

### What the 854 mH implies, derived

With realistic load capacitance the resonance lands at 9.9 kHz (300 pF short
cable), 6.5 kHz (700 pF typical), 4.4 kHz (1.5 nF long cable plus self-C).
Our coilFreq control spans 900-6500 Hz: it covers the typical and long-cable
cases and stops just short of the short-cable one. Acceptable; noted.

The Q is the finding. At resonance X_L is about 35 kohm, so the Stage's 10k
volume pot loads the pickup to **Q ~ 0.3 -- the resonant peak does not exist
on a stock Stage**, which is also what Rob A reports hearing against a 1 Mohm
input. Into the Suitcase's high-impedance preamp the series DCR limits Q to
about 16 and the combined load to roughly 10. Our coilQ control tops out at
5.55: modest shortfall at the bright end, noted. The Stage-vs-Suitcase tone
difference is therefore substantially THE LOAD, with numbers behind it -- a
future "output load" dimension for the Stage DI preset, sourced rather than
voiced.

The humbucking pairing has a modelling consequence beyond noise: whatever
mutual-inductance crosstalk exists between neighbouring pickups is partially
phase-cancelled by construction, which weakens the case for modelling
crosstalk at all.

### Still open

Per-pickup inductance and self-capacitance (Morrin's figure is the whole
harp; the group topology is known so per-unit values could be back-derived,
but a direct LCR reading of a single pickup at 120 Hz -- the guitarnuts2
thread documents why 120 Hz and not 1 kHz, eddy losses in the steel core
suppress high-frequency readings -- remains the clean measurement). The harp
frame tap test remains entirely open.

Sources:
- https://sites.google.com/site/davidmorrinoldsite/home/trouble/trouble-keyboards/rhodes
- https://ep-forum.com/smf/index.php?topic=6237.0
- https://www.fenderrhodes.com/org/manual/ch10.html
- https://guitarnuts2.proboards.com/thread/8072/measuring-pickups-lcr-meters
