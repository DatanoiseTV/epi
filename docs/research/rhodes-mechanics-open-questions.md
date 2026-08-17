# Rhodes mechanics — the three open questions, with numbers

Companion to `acoustic-checklist.md` (targets) and
`transducers-and-chassis.md` (electrical side). This file closes the research
side of the three blocking model corrections: the striking-line
recalibration, the B5 two-slope decay, and the harp-frame modes. Every number
carries a source. Tags: **[M]** measured (here or in a cited paper), **[R]**
read in a primary document, **[C]** computed from sourced numbers, **[E]**
estimate with stated assumptions.

Primary sources on disk (session scratchpad + `rhodesref/`):

- Rhodes Service Manual, CBS 1979, full text (`rhodes_sm.txt`) — ch. 4
  dimensional standards, incl. Figure 4-2 escapement values read from the
  scanned figure.
- Shear, *The Electromagnetically Sustained Rhodes Piano*, MSc thesis, UCSB
  2011 (`shear_thesis.txt`) and the NIME'11 paper (`shear_nime.txt`).
- Muenster & Pfeifle, ISMA 2014 (`isma.txt`); Pfeifle & Muenster, DAGA 2017
  (`daga2017_rhodes.txt`).
- Falaize & Hélie, JSV 2017 port-Hamiltonian Rhodes (`falaize_jsv.txt`).
- Aalborg University Rhodes FD model report (`aau_rhodes.txt`).
- The reference-set analysis tables J8/J9 (`rhodesref/extra.py`,
  `extra_out.txt`) — the same 1977 Mark I sample set the checklist is built
  from, H1 envelope fitted over the **full** sample length.
- `docs/research/cp70-measured.md` §5 — the CP-80 polarisation measurement.

---

## 1. Striking line — the measured constraints for the recalibration

### 1.1 The geometry as documented [R]

Service manual ch. 4 (all figures verified in the on-disk text and scans):

| # | Quantity | Value | Where |
| --- | --- | --- | --- |
| 1.1a | Strike distance, extreme bass | **57.150 mm** (2-1/4") | ch. 4 §5 |
| 1.1b | Strike distance, extreme treble | **3.175 mm** (1/8") | ch. 4 §5 |
| 1.1c | Measured between | leading edge of hammer tip → leading edge of tone generator | ch. 4 §5 |
| 1.1d | Set by | "the precise curve given to the Tone Bar Rail"; re-established by ear at C4, F3, C3 | ch. 4 §5 |
| 1.1e | Key dip | 9.525 ± 0.794 mm | ch. 4 §1 |
| 1.1f | Escapement, ideal | 0.794 mm (1/32") | ch. 4 §2 |
| 1.1g | Escapement, extreme bass | **6.350–9.525 mm** | Fig. 4-2 |
| 1.1h | Escapement, tone bar 41 | **1.588–3.175 mm** | Fig. 4-2 |
| 1.1i | Escapement, extreme treble | **0.794–2.381 mm** | Fig. 4-2 |
| 1.1j | Escapement exists because | "the whipping action of the Tine ... increases as it becomes longer toward the Bass end" | ch. 4 §2 |
| 1.1k | Hammer tips, notes 1–30 | Shore A 30, height 6.350 mm | ch. 4 tip table |
| 1.1l | notes 31–40 | Shore A 50, 7.938 mm | " |
| 1.1m | notes 41–50 | Shore A 70, 9.925 mm (text; 9.525 is the consistent 3/8") | " |
| 1.1n | notes 51–64 | Shore A 90, 11.112 mm | " |
| 1.1o | notes 65–88 | wrapped, "extra hard", 11.112 mm | " |
| 1.1p | Tonebar height range | 4.762–12.700 mm, factory 9.525 mm | ch. 4 Fig. 4-4 |
| 1.1q | Damper clearance | 9.525–12.700 mm | ch. 4 §3 |
| 1.1r | Pickup–tine gap | 1.588–3.175 mm; 0.508 mm feasible mid/upper post-Mar-1972 | ch. 4 volume adj. |
| 1.1s | Tine rest position | slightly above pickup dead center | ch. 4 timbre adj. |

Row 1.1k–o corrects a comment in `src/epi/dsp/RhodesVoice.h` ("the hammers
are graduated in mass, not in hardness"): the manual graduates the tips in
**hardness** (Shore A 30 → 90 → wrapped) *and* height. The plastic hammer
body is one molding across the compass; the mass graduation from tip height
is a fraction of a gram. Real graduation lives in the **tip stiffness**, not
the mass — the opposite of what the model currently does.

### 1.2 Strike fraction and effective mass at the manual's line [C]

Tine data [R]: Ø 1.5 mm cylindrical, lengths 18–157 mm on the 88-key
(27.5 Hz–4.2 kHz), 41 Hz–2.6 kHz on the 73 (Shear §2.1/§3). With the
manual's endpoints and real lengths, the strike fraction β (distance/free
length):

- 88-key A0: 57.15/157 = **0.36**; 73-key E1 (L ≈ 128–135 mm with tuning
  spring): **0.42–0.45**.
- extreme treble, 88-key: 3.175/18 = **0.18**.

So the real line runs **β ≈ 0.36–0.45 falling to ≈ 0.17–0.20**; the model
(`strikeAt = 0.13 + 0.13·reg`) runs 0.13 rising to 0.26 — wrong direction
and wrong magnitude at both ends.

Clamped-free mode-1 shape, tip-normalised (φ(1) = 1):

| β | φ₁(β) | tip/strike amplitude | 1/φ₁² (eff. mass factor) |
| --- | --- | --- | --- |
| 0.13 | 0.0279 | 35.8 | 1281 |
| 0.18 | 0.0523 | 19.1 | 366 |
| 0.26 | 0.1047 | 9.6 | 91 |
| 0.36 | 0.1903 | 5.3 | 27.6 |
| 0.45 | 0.2829 | 3.5 | 12.5 |

Moving the bass strike 0.13 → 0.36 cuts the effective mass the hammer meets
**46×**; moving the treble 0.26 → 0.18 raises it **4.0×**. That is exactly
why dropping the manual's line into the current calibration overshot the
bass (+16 ct permanent sharpness — the swing quadrupled and the stretching
nonlinearity followed) and made the treble clicky.

**The design logic the curve encodes** [C]: evaluating the effective tine
mass at the manual's strike point across the compass (model's own tine
solve, radius taper 0.95→0.65 mm, exponential interpolation of the strike
distance between the two documented endpoints):

| note | f₀ (Hz) | L (mm) | d (mm) | β | eff. mass at strike (g) |
| --- | --- | --- | --- | --- | --- |
| E1 | 41.2 | 180 | 57.2 | 0.32 | 11.1 |
| A1 | 55.0 | 155 | 46.8 | 0.30 | 10.7 |
| A2 | 110.0 | 106 | 28.9 | 0.27 | 9.8 |
| D4 | 293.7 | 62 | 14.6 | 0.23 | 8.5 |
| A5 | 880.0 | 34 | 6.8 | 0.20 | 7.0 |
| E7 | 2637.0 | 19 | 3.2 | 0.17 | 5.5 |

The effective mass at the strike point is **nearly constant, 11 g falling
gently to 5.5 g** — a 2× spread where the current model has a 52× spread
(checklist, "tine swing" gap). A Rhodes hammer is ~10 g (AAU report: 11 g
[R]); the rail curve makes the collision mass ratio ≈ 1 across the whole
compass, so one hammer molding and one velocity law work everywhere, and the
per-register voicing is done by tip hardness. That is the "sweet spot" the
manual describes, and it is the anchor the recalibration should sit on.

### 1.3 Target quantities the recalibrated model must hit

| # | Quantity | Bass (E1–A2) | Mid (~D4) | Treble (A5–C7) | Source |
| --- | --- | --- | --- | --- | --- |
| T1 | Strike distance (mm) | 57.15 → 28.9 | ≈ 14.6 | 6.8 → 3.175 | manual + exp. interp [C] |
| T2 | Strike fraction β | 0.32–0.45 | ≈ 0.23 | 0.17–0.20 | [C] |
| T3 | Hammer mass (g) | ≈ 8–11, constant | " | " | AAU 11 g [R]; §1.2 [C] |
| T4 | Tip Shore A | 30 | 70–90 | wrapped (hard) | manual [R] |
| T5 | Contact time | 10–15 ms [E] | **6.42 ms** [M] | ~0.5 ms [E] | ISMA 2014; ends inferred from C5 |
| T6 | Attack 10–90% | 14–21 ms | 2–14 ms | 0.6–1 ms | checklist C5 [M] |
| T7 | Strike-point swing, ff | ≈ escapement: 6.35–9.5 mm (saturates, manual 1.1j) | < 1.6–3.2 mm | < 0.8–2.4 mm | Fig. 4-2 [R] |
| T8 | Tip swing, ff | 25–50 mm (longest tine "up to 50 mm") | 1–4 mm | < 1 mm | Shear §3.1 [M]; T7×5.3 [C] |
| T9 | H2−H1 swing pp→ff | 16–37 dB (A4) | " | — | checklist [M] |
| T10 | H1 rise, hard bass | +2.2 to +3.9 dB/s for 4–6 s | — | — | checklist B6 [M]; J8 hard layers: F1 +2.37, B1 +3.90, E2 +2.16 dB/s, knee 3.6–6.0 s [M] |
| T11 | Initial sharpness, hard bass | +9 to +29 cents, settled ~200 ms | < 2 c | < 2 c | checklist F1; J9: F1 ff +29.2 c at 10–60 ms [M] |
| T12 | H1 T60 (unchanged) | 28 s (A2) | 19 s (D4) | 5.7 / 1.7 s (E5/C7) | checklist B4 [M] |
| T13 | Q of the fork (unchanged) | — | E♭4 1520, D4 1156, C4 1238, B3 1101, D♭4 1040 | E♭5 2175, E♭6 1761; E♭2 949, E♭3 731 | Shear Tables 2.1/5.1 [M] |
| T14 | Steady after transient | — | 10–14 ms to clean sine | — | ISMA §1/3.2.1 [M] |

Notes on T5: contact time gates the *linearised* contact stiffness through
τ ≈ π√(m_red/k_eff) with m_red ≈ 4.3–4.8 g (T3 vs §1.2 table):
k_eff(mid) ≈ 1.0–1.5 kN/m, k_eff(bass) ≈ 0.4 kN/m, k_eff(top) ≈ 150 kN/m
[C]. The ~400× stiffness span is what the Shore 30 → wrapped graduation
plus the growing contact-patch fraction (10 mm tip on an 18 mm tine)
delivers physically. It cannot come out of a mass law.

Hammer-model literature anchors: AAU: M_h 11 g, Hunt–Crossley α 2.8,
k 1.5e11 N/m^2.8, λ 9e10, v_max 4 m/s [R]. Falaize JSV: M_h 30 g, felt
B 2.5, K 5e5 N/m^2.5 — flagged: those are *acoustic-piano-typical* felt
values applied to a Rhodes, not Rhodes measurements. Piano-felt table
(Chaigne/Askenfelt style, `hammer_table.jpg`): K 4e8/4.5e9/1e12, α
2.3/2.5/3.0 at C2/C4/C7 — the *graduated-stiffness* pattern, present even
in felt.

### 1.4 Recalibration procedure — which law absorbs what

Order matters; each step has a gate before the next.

1. **Geometry first, then freeze it.** Strike distance in millimetres, not
   fraction: d(n) = 57.15 mm · (3.175/57.15)^((n−1)/72) (exponential in note
   number between the two documented endpoints; only the endpoints are [R],
   the interpolation is the one free choice and it is the one that makes
   eff. mass ≈ constant, §1.2). β then falls 0.32→0.17 on its own. Gate:
   effective-mass-at-strike diag lands in 5–12 g monotone, no clamp pinning.
2. **Hammer mass second: make it constant.** Replace
   `0.30·effTineMass clamped [0.6 g, 6 g]` with m_h ≈ 9 g ± the
   `hammerMassNorm` user trim. The 6 g ceiling pin from note 28–71
   disappears because the law no longer chases a 52× effective-mass ramp.
   Gate: mass ratio m_h/m_eff ∈ [0.8, 1.8] across the compass.
3. **Stiffness third, and it becomes graduated.** Contrary to the current
   comment, graduate contact stiffness with the tip table: Shore A 30 → 90 →
   wrapped is roughly a 1 → 50 MPa modulus span; keep the Hunt–Crossley
   exponent ≈ 2 (cylinder-on-cylinder). Calibrate the scale mid-compass to
   the **6.42 ms** ISMA contact time (T5), then set the bass/treble ends
   from the C5 attack times (T6). Gate: contact-time diag 10–15 / 6.4 / ~0.5
   ms.
4. **Velocity map last.** One global map (the current
   v = 0.18 + 5.6·vel^1.7 shape is fine as a starting family), recalibrated
   against T7/T8 (ff bass strike-point swing approaches but does not exceed
   escapement; tip swing 25–50 mm bass, <1 mm top) and T9 (16–37 dB H2−H1
   swing). Gate: A2/A3/A4 rows plus the swing diagnostics.
5. **Re-measure the pickup-gap conclusion.** The "growl is not behind the
   pickup gap" ruling in the checklist was measured with a bass tip swing of
   3.1 mm. After this recalibration the bass swing is an order of magnitude
   larger; the nine-row sweep that ruled the gap out must be re-run, because
   B6/G2 are expected to move for the same reason the ruling was made
   (large swing across a near-cusp field is precisely the growl mechanism —
   ISMA: "best audible in the lower register where the tines have a larger
   deflection" [R]). J8's hard-layer early slopes (+2.2 to +3.9 dB/s) are
   the acceptance test.

Expected to converge: C5 bass (6.3 → 14–21 ms), B6, G2, F1 amplitude, tine
swing monotonicity. Must not move: A1 (integer partials), B4, T13 Qs, E1.

---

## 2. B5 — what the two-slope decay actually is

### 2.1 What the CP-80 measurement established (for the CP, not the Rhodes)

`cp70-measured.md` §5 [M]: every CP string carries two polarisations,
0.5–1.0 cents apart, slow one starting 3–9 dB below the fast, decaying
**4–8× slower** (C4: 8.29 vs 1.06 dB/s, ratio 7.8; single-string notes show
the same pair, so it is polarisation, not unison coupling). The audible
result is **fast-early, slow-late**: the envelope's slope *flattens* at the
knee. JOS (Asymmetry of Horizontal/Vertical Terminations, on disk) gives
the mechanism: bridge admittance is larger normal to the top plate than
along it.

### 2.2 What the Rhodes reference set actually shows [M]

Table J8 (`rhodesref/extra_out.txt`; H1 heterodyne, full-sample fit,
noise+10 dB gate) — digest:

| note | layer | 1-exp rms (dB) | early (dB/s) | late (dB/s) | knee (s) | late/early |
| --- | --- | --- | --- | --- | --- | --- |
| F1 | ff | 5.14 | **+2.37** | −2.28 | 6.0 | — (rise) |
| B1 | ff | 4.03 | **+3.90** | −1.83 | 3.6 | — (rise) |
| E2 | ff | 4.09 | **+2.16** | −1.96 | 5.0 | — (rise) |
| F1 | pp | 1.97 | −1.40 | −7.46 | 16.0 | 5.3 |
| E2 | pp | 1.37 | −1.40 | −8.88 | 22.6 | 6.3 |
| A2 | pp | 2.43 | −1.70 | −7.15 | 21.8 | 4.2 |
| D3 | pp | 1.92 | −2.46 | −9.54 | 18.3 | 3.9 |
| D4 | ff | 1.94 | −3.23 | −10.20 | 11.9 | 3.2 |
| D4 | pp | 2.07 | −2.97 | −9.42 | 15.3 | 3.2 |
| F4 | pp | 2.38 | −3.31 | −9.51 | 12.4 | 2.9 |
| B4 | pp | 1.70 | −4.12 | −12.51 | 11.1 | 3.0 |
| E5 | ff | 0.63 | −9.39 | −23.13 | 4.6 | 2.5 |
| G6 | pp | 0.29 | −15.65 | −14.10 | 1.0 | ~1 |

Two hard facts fall out:

1. **The Rhodes knee runs the other way.** Slow first, then *faster* —
   the slope steepens at the knee, at every note and layer except the
   hard-bass rise regime. A sum of two decaying exponentials (which is what
   two polarisations, two detuned strings, or a tine–tonebar mode pair all
   produce at the pickup) can only *flatten*: the slower term always owns
   the tail. **The CP-80 polarisation recipe is the wrong sign for this
   instrument and must not be ported.**
2. **The knee sits at a roughly fixed depth, not a fixed time.** Knee time
   × early slope ≈ 35–45 dB below peak for everything from A2 to B4
   (bass pp shallower, 22–32 dB), and knee time scales inversely with decay
   rate (E5 4.5 s, G6 1.0 s). A sampling-session artefact (release, fade)
   would cluster at fixed time; this clusters at fixed relative amplitude —
   it is a property of the instrument, of amplitude-threshold type.
3. The hard-bass rows are not a second mystery: the early **rise** of
   +2.2…+3.9 dB/s over 3.6–6 s *is* checklist row B6, and its knee is
   where the swing re-enters the pickup's linear zone. B5-at-ff-bass and B6
   are one phenomenon (pickup compression at large swing), unlocked by the
   §1 strike-line fix, not by any decay mechanism.

### 2.3 What a same-frequency pair can and cannot do — measured limits [C]

Numerical sweeps with the suite's own metrics (`fitDecay` 0.3–3.5 s for B5,
`detrendedSwingDb` 0.5–3.0 s for E1, D4, late T60 held at 19 s):

- Two components at the **same frequency, common phase** (both driven by the
  same strike, which is the physical case): pure knee, zero beating. Best
  achievable B5 residual in the current 0.3–3.5 s window with E1 < 1.5 dB:
  **≈ 0.9–1.1 dB** — still below the 1.4 dB floor. The windows overlap too
  much; any knee big enough for B5 leaks into E1.
- Adding a split (0.5–1.0 cents) only adds slow beating: E1 blows through
  1.5 dB long before B5 reaches 1.4 (worst-phase E1 2.3–5.6 dB at the
  parameter sets that reach B5 ≈ 1).
- Independent random phase per component is forbidden outright: at the same
  frequency, a near-antiphase draw notches the envelope at the magnitude
  crossing (measured 11–25 dB "swing" in the sweep) — permanently, since
  the relative phase never rotates.
- Any second component obeying E2 (≥ 29 dB down) contributes < 0.05 dB to
  either metric. E2 already rules out a CP-style −3…−9 dB partner on this
  instrument.

**Conclusion: the measured B5 target (1.4–5.1 dB) is unreachable by any
two-component model that also satisfies E1/E2 — and the real instrument
does not produce it that way either.** The real residual lives in the late
knee (10–22 s) and, at hard bass, the early rise; both are outside a 4 s
render fitted over 0.3–3.5 s. The reference numbers were fitted over the
full sample length (extra.py), a different quantity than the test measures.

### 2.4 The recipe

**Vertical vs horizontal clamp asymmetry (the question as posed):** the
tine–tonebar fork balances only in the vertical (strike) plane; the
horizontal polarisation has no balancing prong and drives the clamp
directly, so it decays at the *overtone* Q (~70–95 against the fundamental's
1000–2500 [M] Shear) — T60 ≈ 0.5–1 s at D4, i.e. the horizontal is the
**fast** one and it is 20–40× faster, not 4–8×. It is excited at skew ≈ 5%
(−26 dB) and barely sensed (the pickup reads vertical displacement). It can
stay exactly as modelled — same frequency is right; the 1.004× detune
currently in `RhodesVoice.h` should drop to 1.000 (it buys nothing and is
the one residual beat risk). It is not, and cannot be, the B5 mechanism.

**The B5 mechanism to implement:** amplitude-threshold damping on the
fundamental — passive by construction (pure extra loss, no feedback):

```
gamma_1(a) = gamma_B4 · (1 + (r − 1) · s(a_k / a)),   s = smooth step
r   = 3.2 (mid/treble) … 5.3 (bass)      [J8 late/early column]
a_k = H1 tip amplitude 38 ± 5 dB below the note's ff peak amplitude
      (knee then lands at J8's measured 60–80% of the visible span)
split: 0.0 cents.  Level offset: n/a (single component).  No phase freedom.
```

Physical candidates for the threshold loss, in order of plausibility:
friction at the tuning-spring wrap (a tight friction fit on the tine [R],
constant-force loss per cycle → decay steepens as amplitude falls),
micro-slip at the block/tonebar bolted joint, damper felt proximity. The
data cannot separate them and the model does not need to know.

**Predicted numbers** (Coulomb-form simulation, same fitting code as the
reference analysis): D4 full-span 1-exp residual **1.5–1.9 dB** (measured
1.94–2.07); two-slope refit recovers early −3.4, late −6.5…−8.2 dB/s, knee
8–9 s (measured −3.0…−3.2 / −9.4…−10.2 / 12–15 s — tune a_k within its ±5 dB
band to land it); added E1 swing **< 0.05 dB**; B4 fitted T60 unchanged.

**Required test change:** B5 as currently implemented (4 s render, 0.3–3.5 s
fit) reads **≈ 0.3 dB before and after** this fix — the phenomenon is
invisible in that window (§2.3 proves nothing visible there is admissible).
The row must render ≥ 12 s (bass: ≥ 20 s) and fit the full span above
noise+10 dB, exactly like `extra.py`; alternatively assert on the two-slope
decomposition (early slope = B4 rate, late/early ratio 2.9–5.3, knee depth
35–45 dB) which is sharper than an RMS residual. Until the row is
re-windowed, B5 cannot move for any physical reason and should not be
chased.

---

## 3. Harp frame resonances

### 3.1 The hunt — what exists and what does not

Searched this session: full service manual text (all 11 chapters on disk —
construction facts only, no vibration data); ISMA 2014 (measures the
**tonebar**, 51–222 Hz, already consumed by checklist section D); DAGA 2017
and the Springer chapter (tone production, no frame data); Shear thesis
(tine/tonebar Q only); AAU report (tine FD model, no frame); Falaize JSV
(beam + pickup only); Gabrielli et al. (signal-model, no frame); ep-forum
board scan (repair topics only); fenderrhodes.com manual ch. 1 (re-fetched:
no frame acoustics). The one document likely to contain frame measurements —
Wendland, *Klang und Akustik des Fender Rhodes E-Pianos*, TU Berlin 2009
(cited by ISMA [9]) — is unreachable: the TU-Berlin AK site was restructured
(404 on all historical paths) and archive.org returned 502/503 for the whole
session. Retry the Wendland PDF when archive.org is back; it is the single
highest-value fetch left on this question.

**Conclusion: no measured Rhodes harp/frame modal data exists in any
document obtainable so far.** The six modes in `Harp.h` (47, 88, 143, 211,
305, 418 Hz; T60 0.42–0.11 s; 4 kg) remain invented. What follows bounds
them.

### 3.2 Construction facts that constrain the modes [R]

- The Harp Assembly = **steel Harp Frame** + Tone Bar Rail + Pickup Rail,
  the rails seated into the frame and secured by **14 screws** plus two
  metal Harp Brackets (manual ch. 3). Drilling instructions confirm the
  frame is steel; the Harp Supports are "heavy aluminum extrusions".
- The harp mounts to the two supports by **4 screws** plus a pivot link
  (ch. 4 Fig. 4-7) — a stiff, nearly rigid mounting, not a suspension.
- Every tonebar assembly sits on **two adjustable coil springs** on the
  tone bar rail, height range 4.76–12.70 mm (ch. 4) — 73 spring-mounted
  ~0.1–0.25 kg masses distributed along the rail. The lowest "harp" modes a
  tine clamp actually feels are therefore *tonebar-on-springs bounce* plus
  rail bending, not plate modes of a soundboard. ISMA's tonebar
  eigenfrequencies (51–222 Hz across the compass, non-monotonic) sit in
  exactly this band and are the measured lower edge of the ensemble.
- Span ≈ 1.02 m (73 keys); harp assembly mass ≈ 12–17 kg [E: rails ~3 kg,
  frame ~4 kg, 73 tonebar assemblies ~8 kg, pickups/rail ~2 kg].

### 3.3 Beam bounds on the assembled harp [E→C]

Free-free bending of the assembled harp, L = 1.02 m, composite EI from two
steel angle/channel rails plus frame (EI between 3× a 50×25×2.5 mm steel
angle's weak axis and a stiff bound of 6e4 N·m²), distributed mass 12–17 kg:

| assumption | f1 | f2 | f3 | f4 | f5 | f6 |
| --- | --- | --- | --- | --- | --- | --- |
| compliant bound (EI 7.2e3, 17 kg) | 71 | 196 | 384 | 636 | 949 | 1326 |
| mid (EI 2.8e4, 17 kg) | 141 | 388 | 761 | 1258 | 1879 | 2624 |
| stiff bound (EI 6e4, 12 kg) | 244 | 674 | 1321 | 2183 | 3262 | 4556 |

Free-free mode ratios 1 : 2.76 : 5.40 : 8.93 : 13.3 : 18.6. Damping of
bolted steel/aluminium joints: Q ≈ 20–60 typical → T60 = 2.2·Q/f ≈ 0.15–1.3
s at 100 Hz, 0.03–0.25 s at 500 Hz — the invented T60s (0.42–0.11 s) are
inside this band and need no correction ahead of data.

Verdict on the invented bank: the 88–418 Hz members are defensible (inside
the compliant-to-mid band and overlapping the measured tonebar band); the
**47 Hz mode is below the bending bound** and is only defensible if it
represents tonebar-on-spring bounce or harp-on-case rocking — reasonable,
but it is the least supported number in the set. The invented ratios
(47:88:143:211:305:418 ≈ 1:1.9:3.0:4.5:6.5:8.9) are denser than free-free
bending; a mixed population (2–3 bending + torsion + spring-bounce
clusters) justifies density, so no change is recommended before
measurement — the bank stays bounded by E1 and the pedal-bloom behaviour as
before.

### 3.4 Tap-test protocol (for when an instrument is available)

The DI'd model only needs the modes that reach the output jack. Two
recordings separate physical modes from relevant ones:

1. **Setup**: Stage piano, lid off, dampers resting on tines (pedal up),
   output jack direct to interface at fixed gain, plus one accelerometer
   (or phone with a rigid coupling) on the **pickup rail**, mid-compass.
2. **Excitation**: impulse hammer or screwdriver-handle tap, 5 averaged taps
   per point, three points on the **tone bar rail**: behind tonebars ~6,
   ~36, ~66; one tap on a bass tonebar itself; one on a **harp support**.
   Tap vertically (the sensed plane).
3. **Records**: (a) accelerometer IR — the physical mode set; (b) the DI
   output with dampers up (pedal down) — the coupled, sensed mode set;
   (c) DI with dampers down — leakage floor.
4. **Extraction**: FFT ≥ 0.5 Hz resolution, 0–1 kHz; peak list with −3 dB
   widths; per-peak T60 from band-filtered (1/6 octave) energy decay;
   repeat per tap point to tag mode shapes (bass-end vs treble-end
   dominant). Deliverables: ≤ 10 (f, T60, relative dB in DI) triples,
   pedal-up and pedal-down.
5. **Model acceptance**: replace the six invented modes with the measured
   (f, T60) set scaled to pass E1 (≤ 0.6 dB AM on a held note) and
   reproduce the pedal-down chord bloom; the DI-with-pedal recording is the
   direct reference for that gate.

The measurement is ~30 minutes with a real instrument and remains the only
way to close this; nothing publishable was found and the bounds above are
the honest ceiling of what desk research yields.

---

## Cross-cutting: order of work

1. §1 recalibration (geometry → mass → stiffness → velocity), suite as
   judge. This is expected to move B6/G2/C5/F1 and *creates* the large bass
   swing that §2.2's hard-layer rise regime needs.
2. Re-measure the J8 hard-bass rows on the model (render 20 s, full-span
   fit). If the rise appears, B5-hard and B6 close together.
3. §2.4 threshold damping for the soft/mid knee, after re-windowing the B5
   row; verify E1/E2/B4 unmoved.
4. §3 stays parked on the tap test; retry the Wendland PDF via archive.org
   before any further invention.

## Formulas used (for reproducibility)

- Clamped-free mode 1: κ₁ = 1.87510; φ(x) = (cosh κx − cos κx) −
  σ(sinh κx − sin κx), σ = (sinh κ − sin κ)/(cosh κ + cos κ); effective
  mass at x: m_eff = m_modal/φ(x)² with φ tip-normalised.
- Tine length from frequency (Shear eq. 2.1): f = 1.426·πK/(8L²)·√(E/ρ),
  K = r/2, E = 2.0e11 Pa, ρ = 7850 kg/m³.
- Free-free beam: f_n = k_n²/(2πL²)·√(EI/μ), k_n = 4.730, 7.853, 10.996,
  14.137, 17.279, 20.420.
- Contact time (linearised): τ ≈ π√(m_red/k_eff), 1/m_red = 1/m_h + 1/m_eff.
- Coulomb + viscous envelope: A(t) = (A₀ + c/γ)e^(−γt) − c/γ; knee at
  A ≈ c/γ.
