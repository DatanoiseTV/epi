# Grand piano implementation plan

How a 2 m grand goes into this engine. Every number is tagged the way
`docs/research/cp70-measured.md` tags them — **[M]** measured on this project's
own reference sets, **[C]** computed from other numbers, **[R]** read in a
primary document — or **[D]** a design decision this plan makes, with its
reason. Reference set: `sfzinstruments/SalamanderGrandPiano` (Yamaha C5,
2.02 m, 24 roots A0–D#7, 16 velocity layers, 48 kHz), the same control
instrument the CP-70 research ran, now promoted from control to target. The
measurements quoted as [M] below were made for this plan from those files
(scratchpad `cp70b/grand_knee.py`, `sal.json`, `salres.json`), with the CP-80
set retained as the *rigid-termination* control.

The one-sentence version: a grand note is one, two or three stiff strings per
key whose terminations all land on **one finite-admittance bridge** feeding a
modal soundboard — and that single fact is the instrument. The CP-70 proved
the rigid limit: −42 dB beat nulls, pure superposition, no two-stage decay.
The grand is defined by NOT being that: the same string machinery, terminated
on something that moves, produces Weinreich's prompt/aftersound, the
polarisation split, sympathetic resonance and the radiated tone — emergently,
from one coupling. The pickup disappears; the soundboard *is* the transducer.

Companion evidence, all on disk: `docs/research/cp70-measured.md` (C5-grand
control columns), Weinreich (Sci. Am. 1979 + KTH pages), JOS bridge pages,
Ege & Boutillon, Bank TASLP 2010, Stulov (JASA 1995 / Acta 2005), Fletcher &
Rossing ch. 12, Woodhouse Euphonics §7.3/§12.2.

---

## 1. Voice structure

### 1.1 Shape

```
hammer ─> string 1 (V+H) ──┐                            ┌─> modal board (≤1.3 kHz)
       ─> string 2 (V+H) ──┼─> per-note termination     │      │ bridge motion feeds
       ─> string 3 (V+H) ──┘   force F_s = Σ T·kπ/L·q_k ┼──────┘ back into every string
                                                        │
                88 voices ──────────────────────────────┴─> out-of-loop radiator
                                                            (tail >1.3 kHz, stereo)
                                                            + hammer-knock feed -> room
```

- `GrandVoice` holds one to three `GrandString`s (the `CP70String` layout —
  one `SavModalSystem<kMaxStringModes, 2>`, vertical block then horizontal
  block) and one hammer.
- **Unlike the CP-70, the strings are coupled — through the bridge, not to
  each other.** Every string termination reads and writes one shared board
  (section 3). There is no direct string-to-string term; unison interaction
  is emergent from the shared termination, exactly as Weinreich describes
  ("the motion of the bridge causes the vibration of one string to affect the
  vibration of the other" [R]).
- The two polarisations of one string are still not directly coupled; they
  differ in how strongly the bridge lets them couple (section 3.4), which is
  the entire vertical/horizontal story [R].

### 1.2 Strings per note [M][R][D]

```
n_strings = 1 for MIDI <= 39 (A0..D#2)
            2 for 40..51     (E2..D#3)
            3 for MIDI >= 52 (E3 up)
```

Evidence: the complex-exponential fits on the Salamander fundamentals resolve
**two** components at C3 (130.256/130.379 Hz) and **three** at A3
(219.906/220.082/220.141 Hz) [M] — so the 2→3 break sits between C3 and A3.
Broadwood scale table (Euphonics §12.2.1, `piano_table.jpg`): C2 = 1 string,
C3 = 2, C4 = 3 [R]; Bank: "three strings except for the lowest two octaves"
[R]. The exact breaks are per-manufacturer; the chosen ones satisfy every
on-disk constraint and are refinable by component-fitting the remaining
samples (open question 7).

### 1.3 Mode counts [C]

Budget `fs/pi` (15,279 Hz at 48 kHz) as always — the explicit hammer contact
imposes it. B from the measured anchors (section 2), horizontal capped at
1.3 kHz as on the CP-70 (the slow polarisation is a low-frequency phenomenon
there and here; grand aftersound components all measured < 530 Hz [M]):

| MIDI | note | f0 | B [M] | strings | K vert | K horiz | modes/note |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 21 | A0 | 27.5 | 2.40e−4 | 1 | 183 | 55 | 238 |
| 28 | E1 | 41.2 | 1.42e−4 | 1 | 166 | 43 | 209 |
| 33 | A1 | 55.0 | 1.06e−4 | 1 | 150 | 34 | 184 |
| 40 | E2 | 82.4 | 7.9e−5 | 2 | 124 | 23 | 294 |
| 45 | A2 | 110.0 | 8.9e−5 | 2 | 100 | 17 | 234 |
| 52 | E3 | 164.8 | 1.43e−4 | 3 | 70 | 12 | 246 |
| 57 | A3 | 220.0 | 2.16e−4 | 3 | 54 | 9 | 189 |
| 69 | A4 | 440.0 | 6.4e−4 | 3 | 28 | 4 | 96 |
| 81 | A5 | 880.0 | 1.8e−3 | 3 | 14 | 2 | 48 |
| 93 | A6 | 1760.0 | 5.7e−3 | 3 | 7 | 1 | 24 |
| 108 | C8 | 4186.0 | 2.35e−2 | 3 | 3 | 1 | 12 |

- `kMaxStringModes = 240` (A0 carries 183 V + 55 H in one system).
- **Worst case, all 88 notes struck ff under pedal: 10,877 modes** (9,136 V +
  1,741 H). With the vertical cap at 12 kHz [D, mitigation]: 9,458.
- **Sympathetic reduced set** [D]: a string that has not been struck can only
  receive energy through the board, and the in-loop board stops at 1.3 kHz —
  so a pedal-woken string carries only its sub-1.3 kHz modes until its own
  hammer falls. All 88 notes sympathetically live cost **2,308 modes**, not
  10,877. This is not an approximation of the physics; it *is* the physics of
  what the coupling band can deliver.
- Realistic heavy case (10-note ff bass-heavy chord under pedal, all other
  strings sympathetically live): ≈ **3,900 modes**.

### 1.4 CPU verdict [M][C]

The CP-70 benchmark probe measured **3,583 modes = 22 % of a core**, and the
finished CP-70 lands at 24 % worst-case (commit 053b0af) — 163 modes per
percent. Applying that:

| scenario | modes | scalar est. |
| --- | --- | --- |
| all-88 ff pedal glissando (adversarial) | 10,877 | ~67 % |
| same, 12 kHz vertical cap | 9,458 | ~58 % |
| 10-note ff + sympathetic rest (realistic heavy) | ~3,900 | ~24 % |
| plus coupling overhead (two dot products/string, board, radiator) | — | ×~1.25 |

Verdict: **playable scalar** with the sympathetic reduced set and the 12 kHz
cap (adversarial ≈ 72 %, vs the Rhodes' own 60 % worst case), but with no
margin. **The SIMD pass over `SavModalSystem::tick` is scheduled as part of
this instrument, not a prerequisite for starting it** [D]: step 1 of the
implementation order is a probe that measures (a) the 10,877-mode tick and
(b) the coupled two-port's energy behaviour, before any voice code — the same
benchmark-first rule the CP-70 proved twice over. The grand also shrinks
faster than the CP-70: its decay rates are 2–6× higher (section 5), so the
deterministic top-down shrink (reused unchanged) empties the census quickly.
Memory: 88 voices × ≤3 strings × ~33 kB ≈ 10 MB — heap-pooled like `cp70`
(the four-megabyte stack lesson is already paid for).

### 1.5 Unison detune [M][R][D]

Measured pair/triplet splits on the Salamander: C3 0.123 Hz (1.6 c), A3
0.235 Hz (1.9 c), C4 0.188 Hz (1.2 c), C5 0.285 Hz (0.9 c) [M]. Kirk (JASA
1959): listeners prefer **1–2 cents maximum deviation** within the unison
group [R]. Weinreich: below ≈0.3 Hz mistuning the pair locks to one frequency
and beats disappear — the tuner is adjusting aftersound level, not beat rate
[R]. Default: strings at 0 / +1.0 / −0.8 cents nominal, per-note
deterministic scatter 0.5–2 c on the `tipMass` ("Unison") knob — same
mechanism as the CP-70, wider evidence base.

### 1.6 Geometry and modal masses [R][C]

Broadwood grand scale (`piano_table.jpg`, all C's) anchors the geometry:
L = 1013 mm (C1, wound 5.8 mm over 1.21 core) → 639 (C4, plain 1.0 mm) →
324 (C5) → 51 (C8); per-multiplet total masses 189 g (C1) → 0.54 g (C8);
tension ~700–850 N/string mid-compass [R]. Between anchors, plain-wire L
follows the same closed-form derivation the CP-70 research validated
(`L = [π²Ed²/(64ρ f0² B)]^¼` with the measured B — tension must come out in a
smooth 600–900 N band or the fit is wrong, which is the built-in check [C]).
Modal mass μL/2 per mode, pinned-pinned. Wound bass: μ from T = 4f0²L²μ at
the Broadwood lengths. This grand's bass is a real long bass — B *falls* to
7.4e−5 at F#2 [M], the opposite of the CP's ×8 excess.

---

## 2. Mode frequencies

```
f_k = k · f0 · sqrt(1 + B(m) · k²)
```

**B comes from the 24 measured Salamander anchors, log-interpolated** [M] —
the same consume-the-data rule as `CP70Inharmonicity`:

| MIDI | B | MIDI | B | MIDI | B | MIDI | B |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 21 | 2.40e−4 | 42 | 7.37e−5 | 63 | 3.68e−4 | 84 | 2.15e−3 |
| 24 | 1.79e−4 | 45 | 7.87e−5 | 66 | 5.06e−4 | 87 | 2.92e−3 |
| 27 | 1.50e−4 | 48 | 1.09e−4 | 69 | 6.26e−4 | 93 | 5.29e−3 |
| 33 | 9.32e−5 | 51 | 1.27e−4 | 72 | 8.17e−4 | 96 | 8.14e−3 |
| 36 | 9.51e−5 | 54 | 1.70e−4 | 75 | 1.10e−3 | 99 | 9.27e−3 |
| 39 | 8.17e−5 | 57 | 2.16e−4 | 81 | 1.83e−3 | | |

The classic grand V: minimum 7.4e−5 at the wound/plain break (F#2), rising
toward both ends. Extrapolation beyond the anchors: bass slope −0.055/semitone
(a grand's, not the CP's −0.091), treble the universal asymptote [R].

**Stretch** [M]: the Salamander's own measured Railsback curve (from the
per-note fitted f0 column) is the default tuning table: −20.3 c at A0, −12.1
at D#1, −4.9 at C3, −0.9 at C4, +2.3 at C5, +9…+13 c in the sixth octave,
settling ~+2 c at the top. Applied as per-note cent offsets, interpolated;
`tune` and bend on top. Compass: full A0–C8 native — no extrapolated notes
this time; the instrument really has 88.

---

## 3. The bridge: a finite-admittance two-port

This is the load-bearing section. Everything the CP-70 measurably lacks —
two-stage decay, polarisation split, sympathetic answer, energy leaving the
strings — must come out of this one mechanism, with numbers.

### 3.1 What the physics demands [R][M]

- Weinreich, Eb4 (311 Hz): prompt sound decays **≈8 dB/s**, aftersound **at
  less than a quarter that rate**; normal aftersound sits **≈20 dB below the
  prompt level**; stopping one string of a sounding pair makes the survivor
  revert to fast decay with a momentary **+20 dB** jump [R].
- The resistive part of the bridge admittance damps (velocity in phase with
  force); the reactive part pulls frequency. Two identical strings on a
  resistive bridge split into a symmetric mode decaying at 2× the
  single-string bridge rate and an antisymmetric mode the bridge cannot see;
  mistuning below a threshold (≈0.3 Hz mid-keyboard) locks the pair, above it
  beats appear and both decays revert toward the uncoupled rates [R].
- Our own Salamander measurements land exactly there [M]: C4 fundamental =
  (261.290 Hz, 0 dB, **23.3 dB/s**) + (261.478 Hz, **−18.0 dB**, **1.6 dB/s**);
  C5 = 33.6 vs 4.8 dB/s at −14.5 dB; A3 resolves all three normal modes of
  its trichord (9.4 / 3.7 / 1.7 dB/s). Beat minima are **filled in**: C4
  shows a single crossover null −13…−21 dB deep at 1.16 s and nothing else;
  A3 shows ±0.4 dB ripple and no null at all — against the CP-80's −42 dB
  nulls. This is what coupling looks like, and the suite will demand it
  (rows W1–W5, U1).
- Magnitudes: string wave impedance R_s = √(Tμ) ≈ 1.1–2.3 kg/s across the
  compass [C]; board driving-point impedance at the bridge ≈ 800–1000 kg/s
  below 1 kHz, mean mobility Ȳ ≈ 1.3e−3 s/kg, fluctuating ±10–15 dB
  (Ege/Boutillon [R]); Giordano/Wogram: 1–2×10³ kg/s, falling above
  ~2.5 kHz toward 200–300 kg/s [R]. Impedance ratio ≈ 100: the coupling is a
  ~1 % perturbation per reflection, which is what makes the discrete scheme
  below safe.

Sanity chain [C]: single-string bridge-induced decay ≈ 8.686·2 f0 R_s Ȳ →
11.8 dB/s at C4, 16.1 at Eb4 (Weinreich measured ≈8 there on his instrument),
locking threshold σ/π ≈ 0.4 Hz at C4 (Weinreich: ≈0.3 Hz). Adding the
intrinsic loss (section 5) predicts C4 prompt ≈ 18 dB/s single-string,
23–35 dB/s for the in-phase trichord mode — measured 23–29 dB/s [M]. The a
priori numbers land within a factor of ~1.5 everywhere; one global scalar
(`gV`, section 3.3) absorbs the residual and is fit to the knee table.

### 3.2 The board in the loop

One shared `SavModalSystem<kBoardModes = 72, 2>` — the low band of the
soundboard, up to ≈1.3 kHz:

- Mode ladder [R][D]: modal density rising to n∞ ≈ 0.05–0.06 modes/Hz
  (Ege [R]); first modes of Ege's upright 114/134 Hz, scaled to a 2 m grand's
  larger board → first mode ≈ **75 Hz** [D — open question 3, closable from
  the Salamander attack-knock spectrum]. 72 modes covers 75–1300 Hz at the
  measured density.
- Damping: loss factor η ≈ 2 % ± 1 %, no strong frequency trend (Ege [R]) →
  per-mode T60 = 2.2/(η f): 1.3 s at 100 Hz, 0.13 s at 1 kHz. Bank: a
  soundboard response is a sum of damped modes with **no reverb-like
  build-up**; attack noise lasts 300–400 ms [R]. The board is lossy — that
  loss *is* the prompt sound's energy drain.
- Modal masses M/4 with M ≈ 9 kg board mass (Ege [R]) ≈ 2.25 kg.
- Per-note bridge shape Φ(note) — 72 weights, the board's mode amplitudes at
  that note's bridge point [D]: sampled from rectangular-orthotropic-plate
  shapes along a bridge arc, deterministic, sign-varying, normalised so the
  median driving-point mobility 100–1000 Hz equals Ȳ = 1.3e−3 s/kg × `gV`.
  Neighbouring notes get correlated shapes automatically (continuous
  functions), which is what makes sympathetic response selective. The exact
  shapes are unknowable without measuring one board; the statistics are the
  defensible content (Skudrzyk mean-value theory, which Ege validates [R]).

### 3.3 The two-port and its discrete form [D]

Per string, per polarisation, with **w_k = T·kπ/L** — the exact termination
force weights the CP-70 already uses as its piezo readout:

```
each sample, BEFORE either side ticks:
  F_s  = Σ_k w_k q_k                         # string termination force [N]
  board.addForce(m, Φ_m · (F_sV + gH·F_sH))  # drive the board at the bridge
  a_b  = Σ_m Φ_m (q_m − 2q_m⁻¹ + q_m⁻²)/dt²  # bridge acceleration (board history)
  string.addForce(k, −(w_k/ω_k²) · gP · a_b) # reaction, SAME weight family
then both sides tick().
```

- **Why acceleration in, force out**: substituting y = u_b·(1−x/L) + Σ q_k φ_k
  into the string Lagrangian puts the string↔bridge coupling entirely in the
  **mass matrix** (m_ck = μL/kπ ≡ w_k/ω_k²); the stiffness stays block
  diagonal. A mass matrix is a Gram matrix — positive semidefinite **by
  construction** — so the coupled continuous system is Hamiltonian plus
  damping: passive at any coupling strength, with no tuning condition. This
  is the passivity argument, and it is structural, not numerical. The
  per-mode loop weight w_k·(w_k/ω_k²) = Tμ = R_s² is mode-independent [C] —
  the coupling does not grow with partial number.
- **Reciprocity**: read and write go through the same w_k family (the ω_k²
  factor is the physical mass↔stiffness conversion, not a one-sided filter).
  The sense-and-force-through-the-same-transformation rule is project law;
  the one time it was broken cost a metre-deflected tine.
- **Discretely** the exchange is explicit (read both pre-tick, apply equal
  and opposite) — the `Harp` precedent, at a perturbation strength the
  impedance ratio bounds at ~1 % per string round trip. a_b needs no
  smoothing: the board is band-limited to 1.3 kHz by construction, so its
  second difference is clean. The energy row (E1) *measures* the residual
  drift; the step-1 probe measures it at full polyphony before any voice
  code exists.
- **Named escalation** if the probe shows drift at worst case: the coupling
  gradient is constant, exactly the shape of the tine–tonebar quadratised
  joint, so it can move into a SAV slot solved implicitly by the existing
  Woodbury machinery (rank grows by one per sounding string — still tiny).
  Named retreat if even that misbehaves: Bank's structurally-stable
  feedforward coupling (TASLP 2010 §VI [R]) for the top two octaves only,
  where aftersound is measurably absent anyway. Bank went fully feedforward
  because modal bidirectional coupling was "hard to keep stable"; this
  engine's integrator discipline is the answer to that, and the plan says so
  with a measurement gate rather than by assertion.

### 3.4 What then emerges, with targets

- **Two-stage decay of unisons**: in-phase combination decays at ~N× the
  single-string bridge rate + intrinsic; near-antiphase combinations decay at
  nearly intrinsic-only. Targets: C4 fast 23.3 dB/s / slow 1.6 dB/s with the
  slow component −18 dB [M]; knee at 1.6 ± 0.6 s at −25 dB (C4), 1.1 s at
  −41 dB (C3), 1.3 s at −24 dB (C5); gentle-knee notes (D#2, A3) must come
  out gentle too [M].
- **Vertical/horizontal split**: H couples through `gH` ≈ 0.07 [D] (Weinreich:
  bridge "gives much more easily" vertically; initial V ≥ 10× H [R]; JOS
  asymmetry page: |Y_v| ≫ |Y_h| [R]). H therefore keeps nearly its intrinsic
  slow decay — the measured aftersound rates 0.8–3.2 dB/s [M] — while V pays
  the bridge. Hammer skew 0.5 gives H its −6 dB launch, as on the CP-70.
- **Sympathetic resonance**: pedal up-dampers strings whose Φ overlaps the
  struck note's — automatic through the shared board. With dampers down,
  non-struck strings stay clamped (damper factor holds them), so no false
  halo: the same gate the Rhodes damper logic already implements.
- **Energy transfer to the board**: the board's modal loss (η ≈ 2 %) plus the
  radiator drain is where the prompt sound goes; the strings' own T60s are
  NOT shortened by hand (section 5).
- **Beat nulls fill in**: coupled modes have unequal decay rates and pulled
  frequencies, so envelope minima bottom out at −13…−21 dB, not −42 [M].
  Row U1 *requires superposition to fail* — the exact inversion of the
  CP-70's strongest test.

---

## 4. Soundboard output: modal band + tail + radiation

### 4.1 In the loop vs out of the loop [D]

Only the ≤1.3 kHz modal board loads the strings. Above that the tail is
**feedforward**, and this is defensible on measurement, not convenience: Ege
shows the board's mobility becomes smooth above f_lim ≈ 1.1 kHz (modal
overlap ~100 % at 0.8–1 kHz; waveguide regime between ribs above 1.1 kHz)
[R], so a string partial up there sees a *featureless mean resistance* — no
mode to lock to, no frequency pulling, just loss. A frequency-flat load is
exactly a per-mode decay term, so the HF back-reaction is folded into each
string mode's T60 (section 5) and nothing HF needs to be in the loop.
Bank's shipping model does the same split (loop-free radiator; string decays
carry the coupling's only back-effect [R]) at ~30 % of a 2009 core.

### 4.2 The tail and the stereo image [D][R]

- Radiator: a fixed **out-of-loop parallel bank of 128 second-order sections**
  on a log grid 1.2–15 kHz (Bank's "fully modal" radiator option: fixed poles,
  damping consistent with η ≈ 2 %, numerators fit later against the sample
  set [R]), driven by the summed full-band bridge force. Zero latency, no FFT
  machinery, in-idiom.
- Stereo: two decorrelated readout vectors over the board modes + two
  numerator sets in the radiator (bass lobe left-of-centre, treble right, the
  player's image). Room (`Room`) stays downstream as on the other
  instruments.
- **Attack knock**: Bank's measured trick — feed g·F_hammer (g ≈ 0.2)
  directly into the board input [R]. The knock is 300–400 ms of board modes
  [R], velocity- and register-dependent for free. `ActionNoise` keeps only
  the key/action component, dry.

---

## 5. Losses: the two-instrument decomposition

The engine's own data solves the loss-splitting problem cleanly [C]:

- The CP-80's measured per-partial law is Yamaha wire of the same gauge
  family on a **rigid** termination — i.e. it *is* the intrinsic string loss:
  `alpha_intr(f) = 0.393 + 9.23e−3 f − 1.275e−7 f²` dB/s, fast component
  6.5 dB/s below 1 kHz, slow polarisation alpha/r [M].
- The grand's measured band medians minus the CP-80's are the soundboard's
  drain [M−M=C]: +12 dB/s at 880–1760 Hz, +20 at 1760–3520, ≈+2 at
  3520–7040, ≈+7 above 7 kHz — large in the band where the board radiates
  best (F&R: favoured band ~200–2000 Hz [R]) and small in the treble, where
  the measured grand and CP-80 rates nearly coincide (41.9 vs 43.7 dB/s).
- Implementation: string modes get `alpha_intr(f)` as their own T60; below
  1.3 kHz the bridge two-port adds the rest *emergently*; above 1.3 kHz the
  measured difference table is added per mode as the folded HF board load
  (section 4.1). Nothing is fit twice: the modal band's drain is produced by
  the coupling, and rows T1/W1–W4 check the sum, not the parts.
- Grand broadband −20 dB targets [M]: A0 6.7, D#1 7.6, A1 4.85, C2 1.95,
  D#2 3.95, F#2 3.15, A2 4.95, C3 2.0, A3 2.65, C4 1.45, C5 1.15, A5 1.15,
  C6 0.85, A6 0.95, C7 0.70 s. Per-note variance (C2 vs A2!) is real
  mobility fluctuation [R ±10–15 dB]; the tolerance is ±35 %, and the
  deterministic Φ scatter should produce comparable spread — an explicit
  test observation, not an apology.

Shrink and retirement carry over from `CP70Voice` unchanged (deterministic
top-down shrink from `alphaOfMode`, retirement at −100 dB relative to own
peak). The grand retires far sooner than the CP-70 — nothing outlives ~25 s.

---

## 6. Hammer

Reuse `HuntCrossleyHammer`; parameters move to the felt world. Three zones,
log-interpolated between anchors (Hall/Askenfelt via Chaigne–Askenfelt,
`hammer_table.jpg` [R]):

| anchor | K (N/m^alpha) | alpha | mass [R] | contact target [R] |
| --- | --- | --- | --- | --- |
| C2 | 4e8 | 2.3 | ~11.5 g | ≈4 ms |
| C4 | 4.5e9 | 2.5 | ~9 g | ≈2 ms (≈T/2, max efficiency) |
| C7 | 1e12 | 3.0 | ~6 g | ≈1 ms |

- Mass graduation 12 g (C1) → 5 g (C8) (Broadwood/Stulov agree [R]); the
  full mass strikes the mean patch of the whole choir and the force splits
  equally — the CP-70 bichord contact generalised to three. `lambda` ≈ 1.0
  s/m as the felt hysteresis stand-in [D].
- **Stulov escalation, named**: if the 16-velocity-layer A/B shows the
  spectrum-vs-dynamics slope wrong (felt's signature), replace the H-C loss
  term with Stulov's hereditary form — F = F0[u^p − (ε/τ0)∫u^p e^((ξ−t)/τ0)dξ],
  discretised exactly as a one-pole on u^p (Bank/Borin note it drops in after
  the nonlinearity [R]). Full compass tables are on disk (Acta 2005 [R]):
  m0 = 11.074 − 0.074N + 0.0001N² g, p = 3.7 + 0.015N,
  eps = 0.9894 + 8.8e−5·N, tau0 = 2.72 − 0.02N + 9e−5N² µs,
  F0 = 15500·e^(0.059N) N/mm^p.
- Strike point [R]: beta = 0.135 (C1) → 0.125 (C4) → 0.09 (C6) → 0.08 (C8),
  the Broadwood/Stulov graduation — a grand's beta *falls* with register,
  the opposite of the CP-70. Patch: same sinc machinery, identical read and
  write weights; width 10 mm bass tapering to 6 mm treble [D].
- Contact-stability cap (`wMax = 0.06 fs`) carries over; with K = 1e12 it
  will bind in the top octave; the quadratised contact remains the named fix
  if the capped treble reads soft.
- Velocity law: recalibrated against the Salamander's **16 velocity layers**
  (the CP-70 had four); real hammers peak ≈5–6 m/s [R].

---

## 7. Dampers, pedals, una corda

- **Damper line** [D]: dampers end at F#6 (MIDI 90); the top ~1.5 octaves
  ring free (typical grand practice; open question 8 pins the exact note).
  Same one-line gate as the CP-70's A6.
- **Half-pedal** [D]: CC64 becomes continuous. `damperFactor` is computed
  from a damper T60 that interpolates from gripped (0.06–0.3 s by register)
  to free as pedal value rises 0→1, with the knee mapped so 30–70 % pedal
  gives the partial-damping playing range. One mapping function, one test row.
- **Sostenuto** [D]: CC66 latches a per-voice `sostenuto` flag for notes held
  at pedal-down; `applyDamperIfDue` treats it as `held`. Engine event
  handling gains two event types; voices gain one bool.
- **Una corda** [R][D]: CC67. Trichord notes: hammer strikes **2 of 3**
  (force splits over two; the third stays damper-free and is driven only
  through the bridge); bichord: 1 of 2; monochord: softer felt, K × 0.7.
  The measured signature to reproduce (Southampton `unacorda.txt` [R]):
  level drops only ≈1 dB, the un-struck string's bridge force **grows over
  the first seconds**, beating flattens, aftersound rises relative to the
  attack (Weinreich: the third string starts in antiphase — antisymmetric
  motion from the outset [R]). All of that is emergent here or it is wrong —
  row UC1 checks the growth and the flattening.

---

## 8. Deliberately deferred, with the evidence

- **Longitudinal modes / phantom partials — v2** [R]: components 10–20 dB
  below the main sound in the low bass at ff (Podlesak & Lee), phantoms
  ~10 dB below neighbouring partials (Conklin), decaying ~100 dB/s; Bank
  calls them essential to the ff bass "metallic character". They are real and
  they are audible — but only at ff in the bottom octaves, and Bank's own
  recipe (static tension via Σ n²y_n², i.e. exactly this engine's reserved
  Kirchhoff SAV slot, plus a resonant high-pass correction for the lowest
  2–10 longitudinal modes [R]) drops into the reserved term slots without
  restructuring. Decision: ship v1 without, A/B the ff bass against the
  Salamander ff layers, implement in v1.x if the A/B says so. The SAV slots
  are already reserved in the string systems.
- **Duplex/aliquot — v2** [R]: mid/low duplex segments are felt-muted on most
  pianos; only the treble duplex is tuned and free. Its audible contribution
  is treble attack sparkle; the out-of-loop radiator's fitted numerators will
  absorb most of it. Revisit only if the treble attack A/B shows a missing
  ring; then it is 1–2 extra modes per treble string.
- **Room/cabinet**: `Room` stays; `cabMix` defaults 0 (a grand is not an amp).

---

## 9. Engine integration

- `enum Instrument` is `{rhodes, wurlitzer, clavinet, cp70}`; **`grand` is
  appended fifth** [D] — the enum order is documentation-shared and never
  renumbers. The choice-index table becomes `{ rhodes, cp70, grand }`, and
  `instrumentNames` gains `"Grand"` at the END; the selector parameter is
  already last in the layout and `testParameterOrderIsStable` pins it.
- `std::vector<GrandVoice> grand` (heap, ~10 MB), `processGrand(...)` as a
  third dispatch arm; shared event/pedal/keyDown state. The board, radiator
  and knock feed live in the engine, not in voices.
- Config plumbing copies the CP-70 pattern exactly: `GrandVoice::Config`,
  `grandConfig(p)`, memcmp-versioned bounded rebuilds (a 240-mode rebuild is
  ~6× a CP string; budget 4 per block).

### 9.1 Parameter mapping [D]

| param | Rhodes | CP-70 | Grand |
| --- | --- | --- | --- |
| velCurve/hammerHard/hammerMass/escapement/strikeNoise/damperGrip | as now | as now | same roles (felt K factor, mass factor, let-off, action noise, damper grip + half-pedal scale) |
| tipMass | tuning spring | unison spread | unison spread 0–4 c |
| resDamp | tine Q | alpha trim | intrinsic-alpha trim ×0.7–1.5 |
| bodyMix | harp coupling | hidden (0) | **bridge coupling trim `gV` ×0.5–2** — the honest "soundboard" control |
| barCouple/barTune/nonlinAmt | tonebar/bloom | hidden | hidden (nonlinAmt returns with the v2 tension term) |
| pickupPos/Dist, coilFreq/Q/Sat | magnetics | hidden | hidden — there is no pickup at all |
| preampDrive/bass/treble | Suitcase | CP stack | **hidden drive; bass/treble as ±6 dB output shelves** (a mic'd grand still gets EQ'd; drive would be a lie) |
| tremRate/Depth/Stereo | panner | CP panner | hidden |
| cabMix | Rhodes cab | default 0 | hidden |
| spaceMix/Size, outGain, tune | shared | shared | shared |

Per-instrument visibility is `panels.jsx`'s job (the Strings/Bridge panel
precedent); the DSP never reads hidden ones in the grand path; the
every-control-does-something test runs per instrument.

### 9.2 What GrandVoice does per sample

```
1. hammer.tick against patch-weighted mean of struck strings (una corda
   decides which); force splits equally; g·F into the board (knock)
2. per string, pre-tick: F_s from cached w·q; board force accumulation;
   reaction −(w_k/ω_k²)·gP·a_b into modes
3. all strings tick(); board ticks once per engine sample after all voices
4. voice output: nothing — the BOARD is the output; engine reads board
   stereo vectors + radiator(bridge force sum) + knock, then room
5. control rate: shrink, retirement, damper/sostenuto/half-pedal gates,
   sympathetic reduced-set promotion on strike
```

---

## 10. Test plan

`tests/test_epi_reference.cpp` gains `sectionGrand` (rows G*), same
row/verdict machinery. Reference chain: EQ flat, room off. All targets are
this plan's own measurements unless tagged otherwise.

| # | Property | Target | Method |
| --- | --- | --- | --- |
| K1 | B at 8 notes (A0,F#2,C3,A3,C4,C5,C6,C7) | measured table ±15 % | high-partial LSQ, as CP-70 P1 |
| K2 | Partial frequencies | ±3 c to k=8 | same fit |
| K3 | Railsback | A0 −20.3 c, C4 −0.9, C5 +2.3, C6 +13.5, ±3 c | fundamental vs ET |
| T1 | −20 dB times, 8 notes | A0 6.7, D#1 7.6, C3 2.0, A3 2.65, C4 1.45, C5 1.15, C6 0.85, C7 0.70 s ±35 % | broadband peak-hold envelope |
| W1 | C4 two-exp components | fast 23.3, slow 1.6 dB/s ±40 %; slow starts −18 ± 6 dB | complex-exponential fit on fundamental baseband |
| W2 | A3 trichord modes | 3 components, decay spread ≥ 4× | same |
| W3 | C5 pair | 33.6 / 4.8 dB/s, −14.5 ± 6 dB | same |
| W4 | Knee times/depths | C4 1.6 s @ −25 dB; C3 1.1 s @ −41; C5 1.3 s @ −24; ±0.6 s / ±8 dB | two-segment fit on peak-hold envelope |
| W5 | Gentle notes stay gentle | D#2 early ≈ −5 dB/s, knee ≥ 4 s; A3 ripple ≤ ±1.5 dB, no null | same |
| U1 | **Superposition FAILS** | bichord render minus sum of single-string renders ≥ −45 dB rel. peak (CP-70's U3 demanded ≤ −80) | same harness, inverted verdict |
| U2 | Beat minima filled | deepest envelope null in first 4 s of C4 between −10 and −28 dB; A3 none deeper than −3 dB | envelope minima scan |
| U3 | Prompt jump on damping one string | stop string 2 of a ringing C4 pair at t=3 s → level recovers toward fast-decay track (Weinreich +20 dB effect, informational) | scripted damper |
| Y1 | Sympathetic resonance | strike C4 ff, C3 held open: C3 string energy reaches −60…−25 dB of struck peak within 1 s; zero when dampers down | energy telemetry |
| Y2 | Pedal halo | full-pedal chord decays measurably slower broadband than damped (informational) | envelope |
| UC1 | Una corda | third-string bridge force rises over 2 s; C4 envelope ripple depth shrinks ≥ 2×; output level −1 ± 1 dB | scripted CC67 |
| P1 | V/H structure | single-string note (C2) shows 2 components, ratio 4–17× | complex-exp fit |
| H1 | Contact times | ≈4 ms C2 → ≈1 ms C7 ± 40 % | `contactSamples` |
| S1 | Bass spectrum | A0 fundamental 20–30 dB below strongest partial [R F&R] | spectrum at 0.5 s |
| S2 | Knock | board response to strike alone (strings muted) dies in 300–400 ms [R] | envelope |
| G1 | Damper gate | G6 damps on key-up; A6 rings on | envelope pre/post release |
| G2 | Half pedal | CC64 = 64 gives decay between gripped and free, monotone in CC | envelope vs CC sweep |
| G3 | Sostenuto | held-at-press notes ring through CC66, others damp | scripted |
| E1 | **Coupled passivity** | strings+board total energy never rises (beyond 1e−9 relative/sample) in any hammer-free segment, worst-case polyphony | energy trace — the row the whole bridge design answers to |
| C1 | Adversarial CPU | < 0.5 % missed deadlines, 88-note ff pedal glissando at 48 kHz | stress harness |
| C2 | Retirement | no voice outlives 30 s at ff+pedal | energy trace |
| N1 | No NaN / divergence | existing machinery | — |

Row-zero (before any of these): the **step-1 probe** — 10,877-mode tick CPU
and 24-hour-equivalent energy drift of the explicit two-port at worst-case
polyphony. Everything else waits on those two numbers.

---

## 11. Open questions, ranked by risk

| # | Unknown | Risk | Closes with |
| --- | --- | --- | --- |
| 1 | Discrete two-port energy drift at full polyphony | High — the architecture rests on it | Step-1 probe; escalations pre-named in 3.3 |
| 2 | Worst-case CPU / SIMD payoff | High | Same probe; SIMD pass scheduled |
| 3 | Board mode ladder for a 2 m grand (first mode, density onset) | Medium-high — sets the bass bloom and knock | LF peak analysis of Salamander attack transients (on disk, one script) |
| 4 | gV/Φ statistics (per-note mobility spread) | Medium — owns T1's per-note variance | Fit gV to the knee table; compare model vs measured −20 dB spread |
| 5 | gH (horizontal admittance ratio) | Medium — owns aftersound rates | Fit to measured slow-component slopes (0.8–3.2 dB/s across compass) |
| 6 | Hammer K/alpha vs the 16 velocity layers | Medium — attack brightness and its growth with dynamics | Per-partial A/B per layer; Stulov escalation ready |
| 7 | 1→2 string break point | Low-medium | Complex-exp fits at A1, C2, D#2 (samples on disk) |
| 8 | Damper top note | Low | Any grand photo/spec; F#6 until then |
| 9 | Una corda felt factor (K × 0.7) | Low | UC1 A/B against una-corda recordings |
| 10 | Duplex audibility in treble attack | Low | Treble attack A/B after radiator fit |

---

## 12. Implementation order

1. **Probe first**: bare census-sized tick benchmark + coupled two-port
   energy-drift measurement at worst case, on the target machine. Gates the
   architecture; mitigations and escalations are pre-ranked above.
2. `GrandString`/`GrandVoice` + board + two-port, no radiator: rows E1,
   K1–K3, U1–U2, W1–W5, P1.
3. Radiator, stereo vectors, knock feed: S1–S2, T1.
4. Pedals: half-pedal map, sostenuto, una corda: G1–G3, UC1, Y1–Y2.
5. Engine dispatch, selector append, per-instrument panel, hidden-parameter
   discipline: C1–C2, N1.
6. Calibration pass: gV, gH, hammer anchors, velocity law against the 16
   layers — coupled parameters calibrated together, with the suite as judge.
7. SIMD pass on `SavModalSystem::tick` (2-wide double NEON, hoisted
   coefficients are already vectorisation-ready), re-run C1.
8. Presets ("Concert", "Una corda ballad"), UI panel, checklist doc.

Sources: this plan's own Salamander measurements (scratchpad `cp70b/sal*`,
`grand_knee.py`); `docs/research/cp70-measured.md`; the on-disk papers named
in the header; Kirk JASA 1959 (abstract-level only).
