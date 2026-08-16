# Yamaha CP-70/CP-80 — measured

Companion to `yamaha-cp70-cp80.md`, which covers construction from the service
manual and parts list. This file is what could be *measured* rather than read,
plus the two derivations that turn measurements into the numbers the model
needs. Tags: **[M]** measured here, **[C]** computed from other numbers,
**[R]** read in a document, **not found** — looked for, is not there.

**Audio measured**: `sfzinstruments/GregSullivan.E-Pianos`, `CP80/Samples/` —
81 unlooped mono 44.1 kHz files, 21 roots MIDI 27–107, up to 4 velocity layers,
from a CP-80 rented at Christmas 1998. The author's readme states the tone
controls were flat and no velocity filtering was applied, so the chain is LTI
and cannot alter per-partial decay rates. Files end while the note still rings,
so every decay figure below states the dB span actually observed.

**Control instrument**: `sfzinstruments/SalamanderGrandPiano` (Yamaha C5,
2.02 m), run through identical code. An apples-to-apples reference beats a
published curve.

---

## 1. String scaling — solved for the plain wire

### The gauge map [R]

Parts List §12 "Frame Section", p.28. Key numbering: CP-70 key = MIDI−27,
CP-80 key = MIDI−20.

| MWG | dia (mm) | CP-70 MIDI | CP-80 MIDI |
| --- | --- | --- | --- |
| #18 | 1.025 | — | 63 |
| #17 | 0.975 | 65–71 | 64–71 |
| #16½ | 0.950 | 72–77 | 72–77 |
| #16 | 0.925 | 78–83 | 78–83 |
| #15½ | 0.900 | 84–91 | 84–91 |
| #15 | 0.875 | 92–96 | 92–96 |
| #14½ | 0.850 | — | 97–100 |
| #14 | 0.825 | 97–100 | 101–104 |
| #13½ | 0.800 | — | 105–108 |

Both models use the **same gauge at the same pitch** across their whole
overlap. The plain-wire scale design is shared, so CP-80 measurements transfer
to the CP-70 over MIDI 65–100. Plain wire starts at F4 (CP-70) / D#4 (CP-80).

### Speaking lengths, derived [C]

Eliminating tension between `B = π³Ed⁴/(64TL²)` and `T = 4f₀²L²μ`:

```
            ⎡  π² E d²  ⎤ ¼
    L  =    ⎢───────────⎥          E = 2.0e11 Pa, ρ = 7850 kg/m³
            ⎣64 ρ f₀² B ⎦
```

| MIDI | note | f₀ [M] | d (mm) | B [M] | **L (mm)** | T (N) | % UTS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 63 | D#4 | 310.89 | 1.025 | 4.08e−4 | **568.6** | 810 | 43 |
| 65 | F4 | 347.88 | 0.975 | 4.67e−4 | **507.2** | 730 | 43 |
| 68 | G#4 | 413.75 | 0.975 | 6.39e−4 | **429.9** | 742 | 43 |
| 72 | C5 | 523.38 | 0.950 | 7.11e−4 | **367.3** | 822 | 50 |
| 76 | E5 | 660.95 | 0.950 | 1.173e−3 | **288.4** | 809 | 50 |
| 80 | G#5 | 830.53 | 0.925 | 1.843e−3 | **226.8** | 748 | 48 |
| 85 | C#6 | 1107.90 | 0.900 | 2.771e−3 | **174.9** | 750 | 51 |
| 88 | E6 | 1325.72 | 0.900 | 3.239e−3 | **153.8** | 830 | 57 |
| 91 | G6 | 1576.77 | 0.900 | 4.371e−3 | **130.8** | 850 | 58 |
| 95 | B6 | 1991.74 | 0.875 | 5.880e−3 | **106.6** | 851 | 62 |
| 97 | C#7 | 2229.43 | 0.850 | 7.424e−3 | **93.7** | 777 | 60 |

**Closed form**, R² > 0.999, valid MIDI 63–100:

```
L(m) = 665.2 mm · 2^(−(m−60)/13.16)        i.e. L ∝ f₀^−0.912
```

Length falls to 53% per octave — a near-ideal constant-tension treble scale.
Extrapolated: 82 mm at E7 (top of the CP-70), 43 mm at C8.

**Why this is not circular**: the derivation consumes B and d and produces L.
Tension is then an independent by-product, and it lands at **730–850 N across
34 semitones** with stress rising smoothly from 43% to 62% of UTS. A 15% error
in L would put tension out by 32% and it would not sit in a band. That is the
validation, and it also answers the tension question directly: **yes, 600–900 N
per string, with no parameter tuned to make it so.**

### The wound bass — not derivable, but bounded [C]

For a wound string, stiffness comes from the core and mass from core plus
winding, so one measurement cannot separate them. No published CP-70/CP-80 bass
lengths exist; Mapes serves a CP70B product page with no dimensions.

Required linear mass at L = 679 mm:

| bottom note | T | required μ | equivalent solid copper Ø |
| --- | --- | --- | --- |
| CP-70 E1 (41.2 Hz) | 500 N | 160 g/m | 4.8 mm |
| | 900 N | 288 g/m | 6.4 mm |
| CP-80 A0 (27.5 Hz) | 500 N | **359 g/m** | **7.2 mm** |
| | 700 N | **502 g/m** | **8.5 mm** |
| *(2 m grand A0, for scale)* | 800 N | 66 g/m | 3.1 mm |

**Act on this**: a CP-80 A0 at 679 mm needs a 7–8.5 mm outside diameter, which
nobody builds. Either the 679 mm figure is the CP-70's E1, or the CP-80's
lowest strings are longer. Do not assume they are the same.

Implied steel cores, assuming near-flat bass lengths: D#1 1.24–1.35 mm,
B1 1.04–1.13 mm, F#2 0.82–0.90 mm — entirely normal. The CP gets its bass pitch
from **mass, not length**, and pays for it in inharmonicity.

---

## 2. Inharmonicity — the folklore is wrong

B measured across the compass [M], against the C5 grand measured identically:

| MIDI | note | CP-80 B | C5 grand B | **× grand** |
| --- | --- | --- | --- | --- |
| 21 | A0 | 1.92e−3 *(extrap)* | 2.40e−4 | **8.0** |
| 27 | D#1 | 1.22e−3 | 1.50e−4 | **8.1** |
| 35 | B1 | 5.18e−4 | 9.44e−5 | **5.5** |
| 42 | F#2 | 2.18e−4 | 7.37e−5 | **3.0** |
| 50 | D3 | 2.55e−4 | 1.21e−4 | **2.1** |
| 57 | A3 | 3.31e−4 | 2.16e−4 | **1.5** |
| 60 | C4 | 3.44e−4 | 2.91e−4 | **1.18** |
| 65 | F4 | 4.67e−4 | 4.55e−4 | **1.03** |
| 72 | C5 | 7.11e−4 | 8.17e−4 | **0.87** |
| 80 | G#5 | 1.84e−3 | 1.68e−3 | **1.09** |
| 88 | E6 | 3.24e−3 | 3.23e−3 | **1.00** |
| 95 | B6 | 5.88e−3 | 7.06e−3 | **0.83** |
| 97 | C#7 | 7.42e−3 | 8.50e−3 | **0.87** |

**The plain-wire section of a CP-80 is a normal piano** — within ±20% of a 2 m
grand, and slightly *below* the Young/Rigaud universal treble asymptote. Every
bit of the excess lives in the wound bass, monotonically: ×1.0 at F4, ×1.5 at
A3, ×3.0 at F#2, ×8 at D#1. The bass B-slope is twice a grand's
(−0.091/semitone vs −0.055) because the CP holds length roughly constant where
a grand keeps lengthening.

Corroborated by a technician on this instrument in 1998 [R]: *"The strings are
very short and fat compared to any acoustic piano. The scale is more
compromised than any spinet… so loaded with inharmonicity that you can't be
sure of what the pitch is supposed to be."*

**For the model** — no global multiplier; two segments:

```
MIDI >= 63:   B(m) = exp(0.0926·m − 13.64)     universal treble asymptote
MIDI <= 46:   B(m) = exp(−0.09081·m − 4.3496)  CP-80 measured bass
47..62:       additive blend of the two
```

Also [M]: at D#1 the fundamental is **20.6 dB below the 4th partial**. The CP's
bass has essentially no fundamental — a combination of the force-sensing
pickup's +6 dB/oct tilt and the large B. Model the bass excitation accordingly.

Bonus [M]: the measured cents-vs-ET column is this instrument's Railsback
curve as actually tuned, −6 c at D#1 to +40 c at B7 — consistent with the
factory stretch table.

---

## 3. The pickup

### Circuit topology [R] — CP-80 Overall Circuit Diagram, drawing 006806

88 piezo elements, each from a common signal bus to ground. The bus is
**segmented**, with a series **0.022 µF "mixing capacitor"** joining segments
(explicitly shown between elements 82F# and 83G; junction dots also at
49A/50A#). The summed bus leaves on a shielded lead into **C101 = 0.1 µF in
series** to the first preamp FET gate, biased by **R102 = 470 kΩ to ground**.

This settles the 470k/100nF question: the 0.1 µF is a **series coupling cap**,
not a shunt, and being far larger than any element capacitance it does *not*
set the corner. The elements' own capacitance does.

### The transfer function that matters

A piezo in compression is a **charge source proportional to force**, so for a
string terminated at x = 0 under tension T,

```
q(t) = d₃₃ · F(t),    F(t) = T · ∂y/∂x |₀
```

For mode n, `∂y/∂x|₀ ∝ n·Aₙ`, so a termination-force pickup applies an exact
**+6 dB/octave tilt** across the partial series relative to a displacement
pickup. With the ideal struck-string `Aₙ ∝ 1/n²`, the raw pickup spectrum falls
at −6 dB/oct rather than −12. That alone explains much of the CP's bright,
fundamental-poor tone before any tone stack. **Model it as an explicit `n·`
weighting on the modal outputs, not as an EQ.** In a waveguide, this is the
standard result that bridge force is the *difference* of the incoming and
outgoing waves where displacement is their sum.

Electrically it is a plain first-order high-pass, `f_c = 1/(2πR·C_bus)`,
R = 470 kΩ.

### Element capacitance — not found, but pinned

No datasheet for BD500020 exists publicly. The CP-80 must reproduce A0 at
27.5 Hz, so `f_c ≲ 20 Hz`, so **C_bus ≳ 17 nF, i.e. ≳ ~190 pF per element**. A
geometric estimate for a PZT chip of 10–20 mm² × 1 mm at ε_r 1700–3400 gives
150–600 pF, consistent. **Use C_bus ≈ 20–50 nF, f_c ≈ 8–19 Hz**, recorded as an
inference.

Design consequence: the 0.022 µF mixing capacitors are the same order as the
segment capacitances, so they act as deliberate attenuators between keyboard
regions — Yamaha balancing bass/mid/treble output level. Worth reproducing.

### No pickup resonance is needed — established two ways

[M] Pooling all partial amplitudes against *absolute* frequency, removing each
note's own spectral trend and taking the median per 1/6-octave band (control:
same procedure on the grand) leaves **±5 dB of broad structure and no narrow
fixed-frequency peak** from 60 Hz to 15 kHz that the acoustic grand does not
also show. There is a broad dip at 680–860 Hz and a broad lift from 2.2–9 kHz,
consistent with the tone stack plus the force tilt.

[C] A PZT chip of 9–25 mm² × 1–2 mm at Y ≈ 50 GPa has k = 4.5–7.5e8 N/m; loaded
by a 20–100 g termination block that resonates at **12–24 kHz**, at or above
the top of the band. Its thickness resonance is in the hundreds of kHz.

---

## 4. Decay — the missing soundboard, confirmed and qualified

Time for the beat-suppressed upper envelope to fall 20 dB from peak. `>d` means
never reached inside the sample.

| MIDI | note | CP-80 −20 dB | C5 grand −20 dB | file len |
| --- | --- | --- | --- | --- |
| 27 | D#1 | 10.28 | 7.82 | 12.9 |
| 35 | B1 | **>7.9** | — | 7.9 |
| 42 | F#2 | **>7.4** | 3.10 | 7.4 |
| 50 | D3 | **12.46** | ~2.0 | 22.8 |
| 53 | F3 | 6.32 | ~2.2 | 8.6 |
| 57 | A3 | 4.60 | 2.28 | 8.9 |
| 60 | C4 | **>7.0** | **1.42** | 7.0 |
| 63 | D#4 | **>7.8** | **1.20** | 7.8 |
| 72 | C5 | 3.86 | 1.10 | 5.9 |
| 80 | G#5 | 2.24 | ~1.2 | 3.4 |
| 88 | E6 | 1.42 | ~0.9 | 5.1 |
| 95 | B6 | 1.22 | ~0.7 | 2.4 |
| 97 | C#7 | 1.22 | 0.68 | 2.8 |

**The prediction holds, and it is a mid-range effect, not a uniform one.**
Through the tenor and mid (F#2–C5) the CP-80 rings **2× to at least 6× longer**
than a 2 m grand; at C4/D#4 the grand is 20 dB down in 1.2–1.4 s while the
CP-80 is still not 20 dB down after 7 seconds. The gap closes above B6 (×1.1–1.8)
and *inverts* in the deep bass, where a grand's own bass strings couple weakly
to its soundboard and ring 15–20 s.

Longest measured: **D3 broadband T60 ≈ 62 s**, fitted over 26 dB of a 22.8 s
record — the only file long enough to trust. Then A#2 ≈ 54 s, B1 ≈ 41 s.

### Loss law [M], 979 accepted per-partial fits (r² ≥ 0.90, span ≥ 10 dB)

| band (Hz) | n | median α (dB/s) | T60 (s) |
| --- | --- | --- | --- |
| 110–156 | 9 | 2.33 | 25.7 |
| 220–311 | 19 | 2.02 | 29.7 |
| 440–622 | 77 | 4.89 | 12.3 |
| 880–1245 | 110 | 5.82 | 10.3 |
| 1760–2489 | 134 | 13.28 | 4.5 |
| 3520–4978 | 96 | 41.85 | 1.4 |
| 7040–9956 | 22 | 64.48 | 0.9 |

```
α(f) ≈ 0.393 + 9.23e−3·f − 1.275e−7·f²      polynomial, f in Hz
α(f) ≈ 0.207 · f^0.573                      simpler, good above 1 kHz
```

Below ~1 kHz the median flattens at 2–6 dB/s with wide scatter, because that is
where the polarisation split makes "the" decay rate ill-defined. Use the
two-polarisation structure there instead of a single α.

---

## 5. Unison behaviour — it is polarisation, not coupling

### What Weinreich's model gives in the rigid limit

Weinreich (Sci. Am. 240(1), 1979; companion to JASA 62(6):1474 — the JASA paper
itself is paywalled): *"In a piano the two strings do not vibrate
independently. The motion of the bridge causes the vibration of one string to
affect the vibration of the other."* The whole prompt/aftersound mechanism
exists **only because the support moves**, and it is the resistive part of the
bridge admittance that converts symmetric motion into loss.

As bridge mobility → 0, the coupling coefficient, the frequency pulling, and
the bridge-induced decay of the symmetric mode all go to zero. **So on a rigid
termination the two strings do not exchange energy, there is no prompt/
aftersound, and their sum is plain superposition — beating at the detuning,
with nulls going to −∞ at equal amplitude.**

### What the CP-80 actually does [M]

Fitting N damped complex exponentials directly to the fundamental's baseband —
and, critically, running it on single-string and two-string notes side by side,
which the CP allows because it has 1 string at MIDI ≤ 42 and 2 above:

| note | strings | components (f Hz, amp dB, decay dB/s) |
| --- | --- | --- |
| D#1 (k=2) | **1** | (76.919, −4, **1.81**) (76.990, 0, **7.77**) |
| B1 (k=2) | **1** | (122.562, −13, ~0) (122.674, 0, **4.77**) (122.854, −3, **13.18**) |
| F#2 (k=1) | **1** | (92.243, −3, **1.03**) (92.313, 0, **7.97**) |
| C4 (FF) | 2 | (260.804, −9, **1.06**) (260.950, 0, **8.29**) ‖ (261.598, −10, **0.88**) (261.733, −2, **7.02**) |

C4 resolves into **two matched pairs**:

| | pair A | pair B |
| --- | --- | --- |
| slow | 260.804 Hz, 1.06 dB/s, T60 56.5 s | 261.598 Hz, 0.88 dB/s, T60 68.0 s |
| fast | 260.950 Hz, 8.29 dB/s, T60 7.2 s | 261.733 Hz, 7.02 dB/s, T60 8.5 s |
| within-pair split | 0.147 Hz (0.97 c) | 0.135 Hz (0.89 c) |
| fast/slow ratio | **7.8×** | **8.0×** |

Pair-centre separation 0.79 Hz = 5.2 cents: that is the unison detuning. The
independently recorded F layer reproduces the structure.

**And the same within-pair structure appears on notes with only one string** —
F#2: 1.03 vs 7.97 dB/s, ratio 7.7×.

### Conclusions

1. **The CP-80's double decay is a polarisation effect, not a unison effect.**
   Every string carries a fast (bridge-normal) and a slow (transverse)
   polarisation split by 0.07–0.15 Hz (0.5–1.0 cents), the slow one decaying
   **4–8× more slowly** and starting **3–9 dB below**.
2. **The two unison strings do not exchange energy** — two independent, cleanly
   separated pairs, no symmetric/antisymmetric splitting. Exactly the
   rigid-termination prediction.
3. Corroborating [M]: at D3 the fundamental doublet is 146.679/146.768 Hz
   (1.05 c apart) with amplitudes within 0.8 dB, and the beat nulls reach
   **−42 dB** while the beat peaks decay at only ~1.1 dB/s. Perfect nulls at
   equal amplitude are the signature of pure superposition; coupled modes fill
   them in.
4. F#2 shows exactly one component while A#2 and up show two pairs,
   independently confirming the string-count break at **G2**.

**Model recipe:**

```
n_strings = 1 if MIDI <= 42 else 2
per string: 2 polarisations
   vertical:   full termination coupling, alpha(f) from section 4
   horizontal: alpha / 5..8, +0.5..1.0 cents, 3..9 dB below vertical
unison detune (2-string notes): 1..5 cents
NO inter-string coupling. NO shared bridge admittance.
```

Measured detunings: D3 1.05 c, A#2 1.93 c, F3 ~1.5 c, A3 ~0.5 c, C4 5.2 c,
C5 ~1.3 c. This was a badly-tuned 1998 rental; a well-tuned CP sits at the low
end.

---

## 6. Strike position — not found, and the obvious measurement does not work

Not in any CP-70/CP-80 document. The service manual's tone-adjustment pages
cover hammer spacing, let-off, blow distance and damper timing, but not the
striking point as a fraction of the speaking length.

**The measurement was attempted and it fails — do not repeat it.** Fitting
partial amplitudes with `Aₙ ∝ |sin(nπβ)| ×` a smooth trend and scanning β gives
estimates scattered from 0.040 to 0.270 with 3–9 dB residuals. **The control
proves the method invalid**: run on the Salamander grand, whose mid-range
strike point is ~1/8, the same code returns a median **β = 0.055 (1/18)** with
the same residuals. A bridge-force pickup weights partials by *n*, the tone
stack imposes fixed-frequency structure, and a wide hammer smears the comb —
the sin(nπβ) nulls are not recoverable from steady-state amplitudes.

For building: grands generally run β ≈ 1/7–1/9 in the bass and tenor, narrowing
to 1/10–1/12 in the high treble. With a scale as foreshortened as the CP's, the
*physical* strike distance stays roughly constant, so β *grows* toward the
treble. Use β ≈ 1/8 through the tenor with a mild rise in the top octave, and
flag it as the one parameter with no evidence behind it.

---

## 7. What remains unknown

| # | Unknown | Impact | Cheapest way to close |
| --- | --- | --- | --- |
| 1 | Wound-bass speaking lengths (MIDI 21–62) | **High** — B is measured so the sound is constrained, but the mechanism is not | A tape measure on one real CP, or one photo of the harp with a scale reference |
| 2 | Whether the CP-80's A0 really is 679 mm | **High** — see §1; 679 mm demands an impossible string | Same measurement; check which model the figure came from |
| 3 | Strike position β | Medium — sets the attack comb, not pitch or decay | Measure hammer centre to capo on a real instrument |
| 4 | Piezo element capacitance | Low — only sets a corner in 7–19 Hz, inaudible at 27.5 Hz | One LCR meter reading |
| 5 | True T60 beyond ~30 dB | Low-medium — the long-decay claim is safe but 60 s from a 26 dB fit is an extrapolation | Record one CP note to silence |
| 6 | Damper behaviour on release | Medium for playability | Separate task |

Frame tension as a published figure is **not found** and not worth pursuing —
per-string tension is derived and validated. Order of magnitude for the CP-80's
154 strings is **110–125 kN (11–13 tonnes)** against a 121 kg instrument, so
the harp casting carries essentially all of it.

## Sources

- Parts List CP-70/70B/80 (Apr 1982) — https://manuals.fdiskc.com/flat/Yamaha%20Electric%20Grand%20Parts%20List%20for%20CP-70%20CP-70B%20CP-80.pdf
- CP-80 Service Manual (1978.9) — https://manuals.fdiskc.com/tree/Yamaha/Yamaha%20CP-80%20Service%20Manual.zip
- Bremmer RPT, pianotech list, 1 Nov 1998 — http://moypiano.com/ptg/pianotech.php/1998-November/039217.html
- Sullivan CP80 sample readme v1.3 — https://web.archive.org/web/2006/http://www.sullivang.net/samples/cp80.html
- Röslau/MWG wire gauge table — https://www.fletcher-newman.co.uk/index.php?l=page_view&p=piano_wire_gauge
- Rigaud, David & Daudet, JASA 2013 — https://www.institut-langevin.espci.fr/biblio/2020/3/5/916/files/2013_a_parametric_model_and_estimation_techniques_for_the_inharmonicity_and_tuning_of_the_piano.pdf
- Weinreich, Sci. Am. 240(1), 1979 — http://www.nat.vu.nl/~henkb/Betamusica/Weinreich.pdf
- Kuwabara & Nakamura, Acoust. Sci. Tech. 25(6), 2004 — https://www.jstage.jst.go.jp/article/ast/25/6/25_6_413/_pdf
