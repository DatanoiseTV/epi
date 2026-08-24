# Piano soundboard and string–bridge coupling — measured foundations

What is actually known, with numbers, about coupling strings to a soundboard —
the data the grand plan's bridge/board sections must rest on. Tags as in
`cp70-measured.md`: **[M]** measured here, **[C]** computed from other
numbers, **[R]** read in a document. Sources are the papers in the research
scratchpad (Ege & Boutillon ICA/arXiv, Bank thesis + TASLP 2010, Weinreich
SciAm + KTH lecture, Fletcher & Rossing ch. 4/12) and the Salamander C5 grand
samples (Yamaha C5, 2.02 m, 48 kHz stereo, 16 velocity layers) run through the
same analysis code as the CP-80 work.

---

## 1. The bridge as the string sees it: measured mobility Y(f)

The coupling is one number per frequency: the driving-point admittance
Y(ω) = V(ω)/F(ω) normal to the board at the string's termination. Everything
— per-partial decay, detuning, two-stage behaviour — follows from it.

### Magnitudes [R]

| Quantity | Value | Source |
| --- | --- | --- |
| String characteristic impedance Z_s | ~10 kg/s (bass, wound) → ~5 kg/s (treble); ~2 kg/s for a single plain mid wire | Askenfelt 2006, via Ege; [C] below |
| Board impedance at bridge, low freq | ~10³ kg/s (average, 100–1000 Hz) | Wogram 1980 |
| Same, at bridge near a rib | 1–2·10³ kg/s | Giordano 1998 |
| Same, between ribs away from bridge | 0.6–0.7·10³ kg/s | Giordano 1998 |
| Impedance ratio board:string | on the order of **200:1** (grand, vertical) | F&R §12.6 |
| Fluctuation about the mean | **±10–15 dB** (resonance/antiresonance envelope) | Conklin 1996, Ege synth |
| High-frequency fall | ~5 dB/oct above ~1 kHz, to ~160 kg/s at 10 kHz (Wogram; partly artefact), step to 200–300 kg/s in the treble | Wogram, Weinreich 1995, Giordano |

Only four published bridge measurements exist (Wogram 1980, Nakamura 1983,
Conklin 1996, Giordano 1998); Ege calls Conklin's the most reliable. All agree
on the ~10³ kg/s low-frequency plateau.

### Frequency regimes (Ege & Boutillon, upright in playing condition) [R]

1. **< ~200 Hz**: resolved modes. First board modes at **114 and 134 Hz**
   (their piano). Skudrzyk mean-value only approximate here (modal overlap
   < 20%).
2. **~200–1100 Hz**: behaves as one homogeneous orthotropic plate
   (orthotropy ratio of the homogenised ribbed zone only ~1.4). Modal density
   rises slowly to an asymptote **n(f) ≈ 0.05–0.06 modes/Hz**
   (n∞ = 1/19.5 Hz⁻¹ in the mobility paper). Loss factor
   **η ≈ 2% ± 1%**, flat over several kHz. Board mass **M = 9 kg**
   (1.39 × 0.91 m).
3. **> ~1.1 kHz**: half-wavelength = inter-rib spacing p (~12–13 cm); ribs
   confine waves, board becomes a set of waveguides. Apparent modal density
   drops (~4·10⁻³ modes/Hz at 2.5 kHz in one guide); mean impedance falls
   from ~800 to ~230 kg/s at 2.5 kHz (factor ~3.5).
4. **> ~3 kHz**: half-wavelength *in the bridge* reaches the rib spacing; the
   bridge itself starts to "see" the ribs — impedance at the bridge falls
   (Giordano's at-bridge step is at ~2.5 kHz; between ribs it is at ~700 Hz).

### The two-parameter formula for the mean [R]

Skudrzyk mean-value theorem: the geometric-mean real part of Y needs only the
modal density and the mass,

    G_C = Re(Y) = n(f) / (4M)                       (plate)

Ege upright: G_C = (1/19.5)/(4·9) = **1.4·10⁻³ s/kg → |Z| ≈ 700 kg/s** [C],
matching Wogram/Giordano. The resonance/antiresonance envelope follows from
the modal overlap μ(f) = n(f)·η·f:

    G_res ≈ G_C · coth(πμ/2),   G_ares ≈ G_C · tanh(πμ/2)      (Langley)

with a semi-empirical correction for irregular (Rayleigh-distributed) modal
spacing. Modal masses ≈ M_plate/4 for sine shapes. This is the cheapest
defensible synthetic Y(f): mass, modal density, loss factor — three numbers.

### Directions [R]

Conklin (grand, C6 termination): with strings at tension, the **longitudinal**
(string-direction) bridge mobility is comparable to the normal one above
2–3 kHz; stringing lowers the first-mode peaks by ~15 dB and stiffens the
longitudinal direction by 10–15 dB. Vertical/horizontal asymmetry is what
makes the single-string two-stage decay (§4). Sustain pedal changes the bridge
impedance curve by < 1–2 dB (Bank thesis §2.4) — the pedal's effect is
sympathetic strings, not board loading.

### Soundboard mode tables [R]

Nakamura 1983, rectangular upright board without trimming rims (Chladni):

| f (Hz) | 59 | 107 | 150 | 170 | 205 | 245 | 255 | 295 | 325 | 349 | 600 | 1200 |
| mode | [0,0] | [1,0] | [0,1] | [2,0] | [1,1] | [3,0] | [2,1] | — | — | — | — | — |

Suzuki 1986, 6-ft grand: 49.7, 76.5, 85.3, 116.1, 135.6, 161.1 Hz.
Kindel 1989, 9-ft concert grand: 52, 63, 91, 106, 141, 152, 165, 179, 184,
188 Hz (10 modes below 200 Hz → n ≈ 0.06–0.07 modes/Hz, consistent with Ege).
Measured frequencies fall between simply-supported and clamped predictions;
FE models ran 7–12% sharp (ribs modelled too stiff).

Wood (Ege Table 1, for a synthetic board): Sitka spruce E_L = 11.5 GPa,
E_R = 0.47 GPa, G_LR = 0.5 GPa, ν = 0.3, ρ = 392 kg/m³; Norway spruce
15.8/0.85/0.84 GPa, 440 kg/m³. Panel thickness ~8 mm.

---

## 2. Bridge admittance → per-partial decay: the quantitative link

### The formula [C, standard]

String of wave impedance Z_s = √(Tμ), fundamental f₀, terminated on
Y(f) = G(f) + jB(f) with Z_s·|Y| ≪ 1. Reflectance r ≈ 1 − 2Z_sY per bounce,
f₀ round trips per second:

    α_n   = 2 f₀ Z_s G(f_n)      [Np/s]  → dB/s = 17.37 f₀ Z_s G(f_n)
    T60_n = 6.91 / α_n
    δf_n  = f₀ Z_s B(f_n) / π    [Hz]    (springlike B>0 pulls flat)

Per-partial: evaluate G, B **at the partial frequency** — the ±10–15 dB
envelope of Y is what scatters decay times between adjacent partials.
For N unison strings moving in phase at one point, each string sees N·Y:
the symmetric mode decays **N× faster** than a single string; the
antisymmetric modes see a rigid point (§4).

### Worked numbers, C4 (plain wire, L = 0.62 m, d = 1.0 mm) [C]

μ = 6.2 g/m, T = 649 N, Z_s = 2.0 kg/s, string mass 3.8 g.

| assumed bridge Z = 1/G | single string | trichord symmetric |
| --- | --- | --- |
| 500 kg/s | 18.2 dB/s (T60 3.3 s) | 54.5 dB/s |
| 800 kg/s | 11.4 dB/s (T60 5.3 s) | 34.1 dB/s |
| 1000 kg/s | 9.1 dB/s (T60 6.6 s) | 27.3 dB/s |
| 2000 kg/s | 4.5 dB/s (T60 13.2 s) | 13.6 dB/s |

Measured C4 early decay on the Salamander C5 is 10–14.5 dB/s (§5) — inside
the bracket at the stiff (bridge-loaded) end of the measured Z range, as it
should be: the early segment already mixes in partial dephasing and the
second polarization. Max frequency pull |δf| ≈ 0.2 Hz at Z = 800 kg/s — the
same order as deliberate unison mistuning, so bridge reactance is *part of*
the unison detuning budget, not a separate small effect.

A1 wound string (μ ≈ 60 g/m, L ≈ 1.35 m): Z_s ≈ 8.9 kg/s, predicted single
~10.6 dB/s at Z_b = 800 — but measured A1 fundamental decays at ~0.5 dB/s
(§5): at 55 Hz the board is **below/at its first resonance**, Y is
stiffness-dominated and G ≪ mean, so the mean-value estimate does not apply
below ~100 Hz. The decay formula still holds; only the broadband-mean G is
the wrong input there.

### Measured decay vs impedance (Wogram 1981, single test string) [R]

F&R Table 12.3 — same board point, string tuned to peaks vs valleys of the
measured |Z(f)|:

| Point | T60 across tunings |
| --- | --- |
| upper treble bridge (D7) | 0.6 / 1.0 / 1.4 s |
| mid main bridge (D4) | 31 / 2 / 27 / 22 / 41 s |
| bass bridge (C#2) | 2 / **108** / 24 / 36 / 34 s |

Decay at an impedance *peak* (admittance dip) is up to ~50× longer than at a
valley. Any model with a smooth Y(f) misses this scatter; it is the main
audible signature of a real modal board.

### Overall decay ranges [R]

- Initial decay rates: **~4 dB/s at the bass end → ~80 dB/s at the treble
  end**; T60 from 0.2 to 50 s (Martin 1946, F&R §12.6).
- Board impulse response decay: T60(f) of the *board itself* ~0.07 s at
  100 Hz falling to ~0.01 s at 10 kHz (Bank thesis Fig. 6.12) — the board
  rings tens of ms, the strings seconds. Attack "knock" lasts 300–400 ms.

### Coupling strength / mode splitting (Gough 1981) [R+C]

Weak vs strong coupling at a coincidence of string partial n and a board
mode of quality Q_B, effective mass M_eff: strong when
m_string/(n²M_eff) > π²/(4Q_B²). With Q_B = 50 (η = 2%), M_eff = M/4:

- C4 fundamental: 1.7·10⁻³ vs 0.99·10⁻³ → marginally **strong** (splitting
  or veering possible when a board mode lands on f₀);
- C4 partial 4: weak;
- A1 fundamental: 3.6·10⁻² → strongly coupled; bass fundamentals near
  distinct low board modes can split/lock visibly.

Strong-coupling behaviour: symmetric split, both modes at Q = 2Q_B. In the
weak limit frequencies are unperturbed, only dampings redistribute.

---

## 3. Two-stage decay and unison coupling — Weinreich's numbers

All from Weinreich (SciAm 1979 text + KTH lecture, both on disk; the JASA
1977 paper itself is paywalled, but every number below appears in these).

| # | Property | Value | Source |
| --- | --- | --- | --- |
| W1 | Single string (Eb4, 311 Hz), vertical polarization, prompt rate | **~8 dB/s** | KTH Fig. 1 |
| W2 | Same note, aftersound rate | **< 1/4 of prompt** (< 2 dB/s) | KTH Fig. 1 |
| W3 | Initial vertical:horizontal amplitude | **≥ 10:1** | SciAm |
| W4 | Aftersound level below prompt-sound start | **~20 dB** | SciAm |
| W5 | Symmetric (in-phase) unison mode decay | **N× single-string rate** | SciAm/KTH |
| W6 | Antisymmetric mode decay | ≈ 0 through the bridge (limited by internal/horizontal losses) | SciAm/KTH |
| W7 | Frequency-locking threshold, mid keyboard | mistuning ≲ **0.3 Hz**: no beats, frequencies lock; mistuning controls aftersound level | KTH Fig. 8 |
| W8 | Above threshold | beats appear; at 0.64 Hz mistuning the beat period is *longer* than the naive 1/Δf = 1.6 s | KTH Fig. 8 |
| W9 | Wedge one of two ringing strings mid-note | remaining string reverts to fast decay; radiated level **jumps ~+20 dB** | KTH Figs. 5–6 |
| W10 | D#4 (311 Hz) perpendicular vs parallel string decay | perpendicular reaches −80 dB in ~15 s; parallel far slower over the same 20 s window | F&R Fig. 12.32 (Weinreich 1977) |

Mechanism checklist for a model (all three are needed; they are distinct):
1. **Two polarizations** with different terminating G (vertical ≫ horizontal
   coupling) — gives two-stage decay even for a single string.
2. **Unison coupling through a resistive bridge** — symmetric mode dies
   N× fast, antisymmetric survives; hammer imperfection plus mistuning set
   the antisymmetric admixture.
3. **Mistuning + complex Y** — locking below ~0.3 Hz, slow non-exponential
   beats above; bridge reactance shifts both frequencies (springy support
   flattens, massy sharpens — resistive leaves pitch alone and damps).

The eigen-structure (JOS, Physical Audio Signal Processing §C.13): two
identical strings on bridge impedance R_b have eigenvectors [1,1] and
[1,−1]; only the in-phase eigenvalue depends on the bridge,
λ₁ = (R_b − 2Z_s)/(R_b + 2Z_s); the anti-phase mode sees a rigid point
exactly. Aftersound is therefore *in tune with the rigid-termination pitch*
while the prompt sound is pulled — measurably flatter in
stiffness-controlled bands.

---

## 4. Reference measurements: Salamander C5 grand [M]

Files `sal/<note>v14.flac` (velocity layer 14/16, hard; 48 kHz stereo,
natural decay ≥ 15 s, no loop, no release overlay in the sustained region).
Method: mono sum → heterodyne at the measured partial frequency → 3× moving
average (~8 Hz) → dB envelope from 0.1 s after peak until 8 dB above the
noise floor → two-segment piecewise-linear fit (knee scanned, least RMS).
Beats: dominant peak (0.05–6 Hz) in the FFT of the fit residual. Analysis
script: scratchpad `sal_decay.py`.

### Two-stage decay of the fundamental

| Note | f₁ meas (Hz) | early (dB/s) | late (dB/s) | knee (s) | drop at knee (dB) | 2-seg/1-seg RMS (dB) |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | 54.73 | −0.45 | −3.28 | 18.1 | 7.6 | 2.5 / 2.8 |
| C3 | 130.54 | −3.96 | −0.94 | 7.8 | 55.4 | 4.1 / 5.8 |
| C4 | 261.63 | −10.32 | −2.02 | 2.39 | 33.1 | 3.3 / 3.9 |
| C5 | 524.47 | −16.78 | −5.10 | 1.36 | 27.6 | 4.5 / 4.8 |

Second partial (same files): A1 P2 −5.1/−1.1 dB/s knee 5.4 s; C3 P2
−4.8/−3.7; C4 P2 −4.3/−3.7; C5 P2 −13.1/−4.1 knee 2.5 s.

Fixed-window initial rates (0.1–2.0 s, literature-comparable): A1 +0.1,
C3 −11.7, C4 −14.5, C5 −5.1 dB/s (C5's window straddles its 1.36 s knee and
a beat null — trust the two-segment number there).

Readings:
- The early/late ratio is ~4–5× at C4 (10.3/2.0) and ~3× at C5 — matching
  Weinreich's "prompt ≈ 4× aftersound" at Eb4 (W1/W2).
- Knee time falls with register (7.8 s → 1.36 s from C3 to C5) — decay rates
  scale with f₀ (the formula's 2f₀Z_sG) while the antisymmetric floor doesn't.
- A1 breaks the pattern *in the expected direction*: fundamental below the
  board's working range decays at only 0.45 dB/s (T60 > 2 min if it held);
  its P2 (109 Hz) shows the normal two-stage shape. Any model driving all
  partials with a broadband-mean G will kill A1's fundamental ~20× too fast.
- Residual RMS of 2.5–4.6 dB even after the two-slope fit is the beating —
  the envelope is not two clean lines and test tolerances must allow that.

Consistency note (added later, from the numbers above alone; the flac set is
no longer on disk to re-fit): the C3 row's "drop at knee" is inconsistent
with its own fit — early rate × knee time = 3.96 × 7.8 = 31 dB, not 55.4 —
the signature of the two-segment fit latching onto a beat null instead of the
regime change (C3 beats at 0.22 Hz, period comparable to the fit window).
The same failure mode explains the broadband C3 knee "1.1 s @ −41 dB" in the
plan's W4 table, which contradicts the plan's own T1 measurement (broadband
−20 dB at 2.0 s, same files, monotone peak-hold envelope): the depth
consistent with T1 and the C3 late rates above (fundamental −0.94, P2
−3.7 dB/s) is −19.2…−16.7 dB at the 1.1 s knee. Treat C3 knee *depths* from
this set as untrustworthy until the samples return and can be re-fit with a
beat-robust method; the knee *times* and the rates cross-check fine.

### Unison beating

Dominant residual modulation, early segment: A1 4.7 Hz (weak-partner or
polarization beat — A1 may be a single wound string; treat with caution),
C3 0.22 Hz, C4 0.49 Hz, C5 0.98 Hz; whole-file dominant peaks 0.06–0.5 Hz.
Consistent with per-string mistunings of a few tenths of Hz (W7 regime:
partially locked, slow beats). Test row: mid-compass unison beat periods of
**2–15 s**, faster (~1–2 s) at C5 and above.

### Stereo field of the pair (10 s window, L/R of the same files)

| Note | ILD (dB) | coherence 50–200 | 200–500 | 0.5–1k | 1–2k | 2–4k | 4–8k |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | +3.2 | 0.82 | 0.54 | 0.60 | 0.55 | 0.61 | 0.49 |
| C3 | +7.0 | 0.77 | 0.49 | 0.58 | 0.57 | 0.55 | 0.21 |
| C4 | −4.6 | 0.75 | 0.55 | 0.55 | 0.54 | 0.63 | 0.49 |
| C5 | −4.7 | 0.67 | 0.68 | 0.66 | 0.65 | 0.66 | 0.67 |

ILD tracks register (note position along the bridge relative to the mic
pair); interchannel coherence is high (~0.7–0.8) below 200 Hz and drops to
~0.5–0.65 above — the plugin's stereo targets (§6).

---

## 5. Real-time architectures, honestly compared

Target: `SavModalSystem`-style engine (leapfrog, pre-warped, double state,
SAV-quadratised nonlinearities, explicit-contact mode budget f < fs/π).

### (a) Fully coupled modal board (Bank/Zambon/Fontana line)

Bank TASLP 2010's judgement, adopted here: because |Z_board|/|Z_string|
~10³, the *only* audible effects of bidirectional coupling are the changes
in string partial frequencies and decay times. So: keep the board's
**impedance effect inside the string parameters** (α_n, δf_n from §2 per
partial) and run the board as a separate radiator filter. True bidirectional
sample-level coupling in modal form is possible (append board modes to the
SavModalSystem stiffness matrix; a linear symmetric positive-semidefinite
coupling spring keeps the leapfrog's energy argument intact — no SAV needed
for linear coupling) but numerically it buys only what α_n/δf_n already
encode, plus genuine mode splitting near low-frequency coincidences (§2
Gough rows — audible mainly in the bottom octave). CPU: board modes to
2.5 kHz at n ≈ 0.06 modes/Hz is ~150 modes — one more mid-sized voice,
shared across all notes. Passivity: inherited from the linear scheme;
nothing new to prove.

What sample-level coupling *uniquely* adds: sympathetic resonance with real
phase relationships, unison locking emerging instead of being parameterized,
restrike into a still-moving termination, bass-mode splitting. Bank got
sympathetics with **unidirectional** coupling (string outputs → secondary
resonator banks, R = 8 keyboard regions, an 8×8 gain matrix, 64 MACs) —
"structurally stable": stable for any gain choice, since energy only flows
forward. That is the cheap 90% of the audible effect.

### (b) Commuted synthesis (board IR outside the loop)

The board IR is played *into* the string as the excitation (Smith/Van Duyne
1995). Cheapest possible: the "filter" is a wavetable. It requires the whole
chain to be LTI. What it **cannot** reproduce — precisely:

1. A nonlinear hammer (the compression exponent and hysteresis change the
   injected spectrum per velocity; commuting demands a linear, precomputed
   hammer filter per velocity layer — a lookup, not a model).
2. Restrike of a ringing string (the stored IR excitation assumes the
   string starts from rest; a real restrike interacts with existing motion).
3. Nonlinear string behaviour (tension modulation, phantom partials —
   anything that makes the string non-LTI invalidates the commutation).
4. Any change of board state at note time (soft pedal shifting the strike
   line is fine; a board that is still ringing from the previous chord is
   not represented — each note gets a fresh, independent IR copy).
5. Bidirectional effects entirely: no locking, no splitting, no
   restrike-phase dependence; per-partial decays must be baked into the
   string loop separately anyway.

For Epi's engine, whose whole point is nonlinear hammer/contact via SAV,
(b) is disqualified as the *primary* structure — but note that a **radiator
convolution outside the feedback loop** (architecture (a)/(c)) is *not*
commuted synthesis and shares none of these limits.

### (c) Hybrid: modal low + statistical high (recommended shape)

Split the radiator at the measurement's own seam (~1–1.1 kHz, where the
board stops being a plate):

- **Low band, modal**: explicit modes to ~1 kHz (n ≈ 0.05/Hz → ~50 modes),
  frequencies/Q/masses from §1 tables or the Langley envelope; these carry
  the audible peak/dip scatter that Table 12.3 shows matters, and the knock.
- **High band, statistical**: mean-value board — smooth G_C(f) with the
  waveguide fall above 1.1 kHz, implemented as a low-order shaping filter
  plus (optionally) a short FDN or velvet-noise tail for density. Bank's
  FDN recipe if wanted: 8 delays (37, 87, 181, 271, 359, 492, 687, 721
  samples — relative primes, short lines for the sharp attack), circular
  feedback matrix first row [−¼, ¾, −¼, …], one-pole loss filters fitted to
  the octave-band T60(f) of a measured IR, 100-tap shaping FIR, 2nd/3rd
  order correction filters per keyboard region.
- **String decays** always from §2's α_n(G) with G evaluated per partial on
  the *modal-low* + *mean-high* composite curve — one consistent Y(f) for
  both the string losses and the radiator, or decays and tone will disagree.

Reference CPU points (Bank TASLP 2010, Core 2 Duo 2.4 GHz, 2010): full
polyphony = ~10 000 second-order resonators + four 20 000-tap partitioned
convolutions at ~30% load; the four convolutions ≈ ¼ of the string+hammer
cost; a 20 000-tap partitioned convolution at N = 128 ≈ 633 flops/sample.
FIR truth: 1000–2000 taps for tonality, tens of thousands for the knock.
Alternative to convolution: fixed-pole parallel second-order filters, 100
log-spaced poles (order 200, pole radius from R = 0.98 damping rule),
zeros solved least-squares — same structure as the string model, and the
free-parameter fit is linear. On 2026 hardware none of these budgets is a
constraint; the choice is architectural, not computational.

Attack knock (Bank): feed **g·F_hammer with g ≈ 0.2** into the radiator
input alongside the bridge force — gives velocity- and register-dependent
knock for free. Worth copying regardless of architecture.

### Passivity summary for the SavModalSystem engine

- Linear board modes in the same leapfrog scheme: passive by the existing
  argument; linear coupling springs are quadratic energy terms — no SAV.
- Per-partial added damping α_n ≥ 0 whenever G(f_n) ≥ 0: passivity of the
  *derived* string losses is just positive-realness of the Y used. Enforce
  G ≥ G_floor > 0 when synthesizing Y from mode tables.
- Unidirectional sympathetic coupling (Bank): unconditionally stable, not
  passive in the physical sense (it injects copies of energy); keep the
  gains small and it is a colour, not a stability question.
- Radiator filters outside the loop: cannot affect stability at all.

---

## 6. Radiation and the plugin's stereo field

Known physics [R]:

- Coincidence: for the spruce/homogenised zone the dynamical rigidities are
  ~150 (ribless corners) and ~100 m⁴s⁻² (ribbed zone) in the along-grain
  direction → coincidence at **1.5 and 1.8 kHz**. Below the lowest board
  modes and above coincidence the board radiates well; ribbing pushes the
  first waveguide mode's supersonic transition up to **~7 kHz** (m = 1;
  m = 2 supersonic from its onset) — the ribs deliberately *extend* the
  subsonic (inefficient, long-sustain) region.
- Favored radiation band **~200–2000 Hz** (Wogram): below, radiation
  efficiency collapses (bass notes are carried by their upper partials —
  Lieber's 88-note SPL curves confirm the balance); above, internal losses
  eat it.
- Radiation patterns are per-mode and interference-dominated above ~200 Hz
  (Suzuki's intensity maps show negative-intensity regions: parts of the
  board *absorb* what other parts radiate). No tractable closed form.
- Vertical and horizontal string polarizations radiate through measurably
  different board patterns — mic-position-dependent interference at the
  prompt/aftersound crossover is real (Weinreich SciAm experiment).

Defensible plugin approach [C from §4 stereo rows]:

1. Two decorrelated radiator filters (L/R) sharing the modal-low section but
   with independent high-band realizations, targeted at the measured
   coherence: **~0.75–0.8 below 200 Hz, ~0.5–0.65 above** — i.e. mostly
   common low end, half-decorrelated mids/highs.
2. Per-note pan by bridge position: ILD of roughly **±3–7 dB** from bass to
   treble (Salamander mic geometry; scale to taste).
3. Prompt/aftersound spatial motion: give the two polarizations slightly
   different L/R filter weights — reproduces the observed
   position-dependent crossover interference with zero extra machinery.

Overreach (not justified by any data on hand): full directivity simulation,
modal radiation efficiencies per mode, room/lid modelling inside the
instrument plugin, HRTF anything. The stereo image of every commercial
reference is a mic-pair recording; matching measured mic-pair statistics is
the honest target.

---

## 7. Confidence ranking and cheapest closing measurements

**High confidence (multiple independent sources, use as-is):**
- Board:string impedance ratio ~10²–10³:1; |Z| ≈ 10³ kg/s at bridge below
  1 kHz; ±10–15 dB envelope. G_C = n/4M with n ≈ 0.05–0.06 modes/Hz,
  η ≈ 2%, M ≈ 9 kg (upright; grands: same n, larger M).
- Two-stage decay and unison mechanism (Weinreich): W1–W10 table.
- Salamander decay/knee/beat/coherence rows (§4) — measured here, script on
  file, re-runnable.
- α_n = 2f₀Z_sG(f_n) as the decay link (textbook; bracket agrees with §4).

**Medium confidence:**
- Exact Y(f) of the *reference* C5 grand — inferred from upright data plus
  Conklin's grand curves, never measured on a C5. Cheapest closer: fit
  G(f_n) per note from the §4 measurements themselves by inverting
  α_n = 2f₀Z_sG — a G-map along the bridge from samples alone (script
  exists; one afternoon).
- Frequency-locking threshold 0.3 Hz (Weinreich's calculation for mid
  keyboard, one bridge impedance). Cheapest closer: measure beat-vs-locked
  behaviour across the Salamander compass from the §4 residuals (same
  script, longer windows).
- Gough strong-coupling boundary for bass fundamentals (computed with
  guessed M_eff, Q_B). Cheapest closer: look for split/locked doublets in
  the lowest Salamander octave's fundamental bins (FFT zoom, minutes).

**Low confidence / open:**
- A1 anomaly attribution (board below first mode vs single-string
  polarization effects) — closable by repeating §4 on A0–C2 and checking
  whether the slow-fundamental pattern tracks the wound single-string range
  or the sub-100 Hz range.
- The 4.7 Hz A1 modulation — polarization beat, longitudinal sideband, or
  analysis artefact. Closer: re-run with narrower demod bandwidth (2 Hz)
  and on the L/R channels separately.
- Horizontal-polarization admittance magnitude (only Conklin's C6-point
  curves exist). No cheap closer from samples; treat the
  vertical:horizontal decay ratio (~4×, W1/W2 and §4 early/late) as the
  calibration target instead of the underlying G_h.

## 8. Per-note mobility spread: measured, priced, not adopted

Open question 4 (the per-note spread of the radiated transduction, rather
than the compass-smooth mean the radiator's direct branch uses) was
implemented twice and measured against the full grand suite. Both
variants were reverted; this section records what they cost and bought,
so the next attempt starts from the numbers instead of the idea.

**The hypothesis is confirmed.** Row T1's note says A3 reads a few dB
weak in its late field because its bridge point sits in a mobility dip
and the read uses the mean. Giving the read the note's own transfer
closes exactly that gap: A3's -20 dB time moves from 1.60 s (gap, band
1.72..3.58 s) into the band. The mechanism named in the row comment is
the right one.

**Variant A -- driving-point receptance.** Read gain per partial =
|omega H(f)| at the note's bridge point over the Skudrzyk mean the shape
amplitude was built around. Result: `fail=3 gap=6`. U2's C4 null
deepened -5.2 -> -8.3 dB (toward its -10..-28 band), A3's decay barely
moved (1.60 -> 1.65 s), the una corda ripple ratio got worse
(1.74x -> 1.40x), and three passing rows broke: W2 (A3 trichord spread),
W5 (D#2 knee 3.9 s vs >= 4), T1 (A0 -20 dB 4.00 s vs 6.70 +/-35%).

The physical objection to A: a radiated read that follows the DRIVING
POINT counts modes that radiate nothing. Below coincidence -- where the
whole in-loop band sits -- what leaves the board is its volume velocity.

**Variant B -- volume-velocity (monopole) transfer.** Weight each mode by
Phi_m(x_bridge) * <Phi_m> instead of Phi_m squared, with
<Phi_m> = phiAmp (2/(p pi)) (2/(r pi)) for odd p, r and exactly zero
otherwise (an even index moves as much plate up as down); normalise by
the RMS of the same quantity over a 16 x 24 grid of bridge points and
in-band frequencies, computed once per board configuration, so the
compass-mean level is unchanged by construction and only the spread is
new. Result: `fail=10 gap=3` -- three gaps CLOSED including T1's A3, and
ten rows broken:

| row | target | measured |
| --- | --- | --- |
| W2 A3 trichord components | >= 3, spread >= 4x | 2 components |
| W4 C3 knee time | 1.1 s +/-0.6 | 2.55 s |
| W4 C5 knee depth | -24 dB +/-8 | -39.8 dB |
| W5 A3 fundamental | dip > -3 dB | -5.7 dB |
| T1 C5 / C6 -20 dB time | 1.15 / 0.85 s +/-35% | 0.70 / 0.45 s |
| S3 ILD A1 (bass lobe left) | +0.5..+7 dB | -0.4 dB |
| UC1 ripple / level | >= 2x, -1 +/-1 dB | 1.0x, -3.4 dB |
| MS10 mic seat band split | each +/-2 dB | -4.23 dB low band |

**The price, stated plainly.** Every one of those rows was calibrated
against the smooth-mean read: the decay tables, the knee family, the
stereo ILD law, the una corda pair, and the mic stage's seat gauge. A
per-note read is not a local change -- it re-derives the compass level
and decay calibration, and the mic gauge with it. That is a campaign
with its own measurement pass, not a patch. Until it is run, the mean
read stays and T1/U2/W5/UC1 stay bounded gaps with this section as
their price tag.

Reproduction: variant B is about forty lines -- a `radiatedTransfer` on
GrandBoard next to `receptance` (same loop, `phi[m] * modeMean[m]`,
skipping zero-volume modes), the grid-RMS normaliser computed where
`thunkPhi` is filled, and one factor on `S.readShape[idx]` in
GrandVoice's mode placement, applied to the coupled prefix only and
above the ladder's first mode.
