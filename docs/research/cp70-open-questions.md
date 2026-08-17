# CP-70 open questions — closed, narrowed, still open

Follow-up to `cp70-measured.md` §7 and the plan's ranked table
(`cp70-implementation-plan.md` §12). Each numbered section takes one open
question, states what new evidence was found, with what confidence, what
remains, and the cheapest closing measurement. Tags as in the companions:
**[M]** measured, **[C]** computed, **[R]** read in a primary document,
**[D]** decision. New sources consulted: the CP-80 Service Manual page scans
(all 52 pages, including the 11×17 fold-outs), the Electric Grand Parts List
(all 38 pages), the CP-70B Operating Manual (18 pages, incl. full spec sheet
and overall circuit), the CP-80M Owner's Manual, the tjscientist
Yamaha-Preamp-Work repository (redrawn schematic + TINA-TI study), the CP-70
Wikipedia article's citations, and the Bremmer pianotech thread.

---

## 1. Wound-bass lengths and the 679 mm contradiction — provenance resolved, windows closed

### Whose length is 679 mm? The CP-70's. [R]

The figure traces to *Classic Keys* (Lenhoff & Robertson 2019), p. 326, via
the CP-70 Wikipedia article: *"the bass strings are very short compared to an
acoustic instrument, 26 3/4 in instead of around 7 ft"* — cited in a sentence
whose subject is the **CP-70**, in the book's CP-70 section. Nothing ties
26 3/4 in to the CP-80. The "impossible string" in cp70-measured §1 was an
artefact of applying a CP-70 number to a CP-80 A0.

Two independent checks corroborate:

**Case dimensions bound the speaking lengths** [R] — both spec sheets give the
harp-section (upper body) case:

| model | upper body case (W × D × H mm) | source |
| --- | --- | --- |
| CP-70B | 1296 wide, profile 901 × 461, 173 deep | CP-70B OM spec, p. 12 |
| CP-80 | 1460 × 1006 × 173 | CP-80 SM General Specifications, p. 2 |

A 679 mm E1 plus tuning pins, capo, hitch and margins is ~850–880 mm of harp —
it fills the CP-70's 901 mm case almost exactly, meaning the bass strings sit
at the physical maximum and the length curve **saturates toward the bottom**
(flat-ish through the wound singles, as compact pianos do). The CP-80's case is
105 mm deeper and 164 mm wider: room for an A0 of roughly **780–870 mm**, and
no more.

**Buildability + measured B close the window** [C] — solving
`B = π³Ed_core⁴/(64TL²)` and `μ = T/(4f₀²L²)` simultaneously against the
measured B law:

| string | L (mm) | T (N) | μ (g/m) | core (mm) | OD (mm) | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| CP-70 E1, B = 1.01e−3 | **679 [R]** | 440–600 | 140–190 | 1.25–1.30 | 4.5–5.2 | buildable, spinet-class |
| same, at the plan's T = 800 N | 679 | 800 | 256 | 1.40 | ~6.1 | implausibly fat |
| CP-80 A0, B = 1.92e−3 (extrap) | 800–870 | 400–600 | 205–310 | 1.50–1.75 | 5.4–6.7 | buildable double-wound |
| CP-80 A0 at 679 mm | 679 | 500–700 | 359–502 | 1.46–1.59 | 7.2–8.5 | nobody builds this — ruled out |

**Consequence for the plan (§1.6):** the wound-bass tension is **not** in the
plain-wire 730–850 N band. A buildable E1 at 679 mm wants **T ≈ 450–600 N**,
μ ≈ 140–190 g/m, modal mass μL/2 ≈ **48–64 g** — not the 87 g the plan's
T = 800 N assumption produces. Short scales drop bass tension; the CP is no
exception. Use T ≈ 500 N as the wound-section default and keep the log-linear
L interpolation, flattened into the bottom five singles.

Also found [R]: the parts list gives **per-key bass string part numbers**
(CP-70: NB500890 "Key #1 E" upward, Left/Right variants from key 16 G — the
bichord break at G2 again), but the numbers are sequential and encode nothing.
CP-70, CP-70B and CP-80 use **three different bass string sets** (NB500880 /
NX502890 / NX502900) — the 70B restring is a genuine respec, so 1976 CP-70
measurements do not automatically transfer to a 70B.

- **Confidence:** provenance high; T/μ/OD windows medium-high (rest on OD
  bounds of real string making plus measured B).
- **Remains:** the actual per-key length and winding schedule.
- **Cheapest closure:** Schaff Piano Supply holds the CP wound-string winding
  specifications (Bremmer, pianotech 1998 [R]) — one request gives per-key
  core/wrap diameters, hence μ exactly, decoupling μ from L. A tape measure on
  one instrument remains the direct alternative. Mapes serves a CP70B page
  with no dimensions; freesound/archive.org have nothing.

---

## 2. Strike position β — still open; the documents genuinely do not contain it

Every plausible document was checked page by page: the CP-80 SM tone-adjustment
chapter (pp. 15–16), assembly/disassembly chapters, the parts-list action and
frame drawings (perspective only, no dimensions), both operating manuals, and
the Arturia CP-70 V manual. **No hammer-line-to-capo dimension exists in any
of them.** The regulation dimensions that *are* documented (and were not in the
research docs before) parameterize the action model:

| quantity [R] | value | SM page |
| --- | --- | --- |
| Cord (blow) striking distance | **40 mm** | 16 |
| Hammer approach (let-off) | 2.5 mm bass / 2.0 mm mid / 1.5 mm treble | 16 |
| Hammer return (drop) from closest point | 2 mm | 16 |
| Hammer stop rail distance | 14 / 13 / 12 mm (low/med/high) | 16 |
| Key dip | 10 mm; white key height 64 mm | 15 |
| Jack play | 0.1–0.2 mm | 15 |
| Damper finish | screwdriver adjustment, no dimension given | 16 |

Also [R]: hammers come in graded groups, not per-key — CP-70/70B keys 1–25 /
26–50 / 51–63 / 64–73 (CP-80: six groups to 88). The plan's log-interpolated
hammer mass should become a 4-step table when calibration data exists.

Weak geometric bounds that survive scrutiny [C]: the strike line is a straight
rail parallel to the keyboard while L collapses 8× across the compass, so the
physical strike distance is near-constant and β must grow toward the treble —
the plan's direction is right. At E7 (L ≈ 82 mm) a 12 mm hammer crown cannot
strike closer than ~0.07 L and plausibly sits at 0.15–0.25 L.

- **Confidence:** that the documents lack it — high (exhaustive). The β = 1/8
  default remains evidence-free.
- **Remains:** the number itself.
- **Cheapest closure:** one photo of any CP-70 harp from above with anything of
  known size in frame (the 40 mm blow distance itself, a coin, the 173 mm case
  rim), measuring hammer line to capo at 3–4 notes. Until then row S2
  calibrates it, as planned.

---

## 3. Piezo element capacitance (BD500020) — still open, inference unchanged

The parts list confirms the element part number and a supersession
(**BD500510 → BD500020**, CP-70B rows, Frame Section p. 28) but gives no
electrical data; no service bulletin or cross-reference is fetchable. The
CP-70B overall circuit [R] confirms the bus interface exactly as inferred:
C101 = 0.1 µF series into FET1, R102 = 470 kΩ to ground — so the corner is set
by element capacitance, and the **C_bus ≈ 20–50 nF, f_c ≈ 8–19 Hz** window
(≥ ~190 pF/element) stands unimproved but uncontradicted.

- **Confidence:** window medium (physics-bounded, twice-derived).
- **Cheapest closure:** one LCR reading on any element. Impact stays low —
  the 12 Hz HP ships either way.

---

## 4. Dampers — top note now verified from the parts list; release behaviour still open

**Dampers stop at A6 = MIDI 93, both models — closed.** [R] The parts list
"Damper Head Assy" tables (pp. 18–20) list one damper head per key and simply
end: CP-70/70B at **Key #66 A** (66 + 27 = MIDI 93), CP-80 at **Key #73 A**
(73 + 20 = MIDI 93). No damper part exists above A6 on either model. The
plan's §7 gate is confirmed at full parts-list precision.

New damper construction detail [R], same pages:

- **Damper heads are per-key graduated parts** (unique part number each key).
- **Three felt geometries** (CC900030/40/50): a U-channel, a wedge, and a flat
  pad — the standard single-string block / bichord wedge / treble flat set.
  Key ranges per felt are not printed; the wedge necessarily serves bichords.
- **Damper lever assemblies come in three grades**: CP-70/70B keys 1–25 /
  26–50 / 51–66; CP-80 1–32 / 33–60 / 61–73 — grip graduation is stepwise,
  three steps, not continuous.
- The damper rides on the key back via a lift post (key assembly drawing,
  p. 23) — a fast, short-throw damper train, matching the CP-70B spec's
  *"modified dampers for fast action"*.

- **Remains:** release timing, half-pedal behaviour, re-grab of a ringing
  string — nothing in any manual quantifies felt contact.
- **Cheapest closure:** unchanged — one recording of staged key releases on a
  real CP (research unknown #6). Launch with Rhodes semantics as planned.

---

## 5. True T60 beyond ~30 dB — still open; the searched avenues are now enumerated

Nothing longer than the Sullivan set's 22.8 s D3 was found: freesound has no
unprocessed CP-70/CP-80 note (145 hits are synths, FX mangles, unrelated
pianos); archive.org's only literal hit ("cp-70-A4") is a Radio Playback India
broadcast, not a note; pianobook's search is not fetchable headlessly; Mapes
and Schaff publish no audio. The 62 s broadband D3 T60 therefore stays a
26 dB-span extrapolation — safe for the model (the fitted slow-polarisation
alphas drive it), soft as a checklist number.

- **Cheapest closure:** unchanged — record one bass note to silence on any CP
  (a phone in a quiet room suffices; the fit needs 40+ dB of span, not
  fidelity). Alternative: ask on an EP forum for a single sustained note; the
  measurement takes one minute of a stranger's time.

---

## 6. Preamp mid scoop and tremolo — closed as filter targets

### The measured curve, digitized [M from the TINA study plot]

Source: tjscientist Yamaha-Preamp-Work — TINA-TI simulation of the full
redrawn CP-80 preamp, output at VM1, **Brilliance HIGH, bass/treble at 99%**,
MID stepped 1→99%. The response plot was pixel-digitized against its grid
(357 px/decade, 17.1 px/dB; the axes verify against the labelled decades).
Controls-flat (MID 99%) response **relative to 10 kHz**:

| Hz | 13 | 20 | 35 | 45 | 60 | 105 | 130 | 180 | 250 | 400 | 500 | 700 | 1k | 2k | 3k | 5k | 10k | 20k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| dB | +2.1 | +2.2 | +1.1 | +0.1 | −1.5 | −5.5 | −7.2 | −9.7 | −11.9 | −14.2 | **−14.5** | −13.7 | −11.8 | −6.9 | −4.3 | −1.7 | 0 | +0.4 |

Broad minimum −14.5 dB at ~500 Hz; response maximum at ~13 Hz (then the input
HP takes over); absolute insertion loss of the passive stack ~−10 dB (gain
staging, irrelevant to shape).

### Corrected filter targets [C]

Least-squares fit of the plan's peaking + two-shelf shape to the 29 digitized
points, 13 Hz–20 kHz:

```
peaking cut:  fc = 488 Hz, −13.4 dB, Q = 0.27
low shelf:    +4.0 dB below 63 Hz
high shelf:   +1.9 dB above 2.9 kHz
max residual 0.39 dB, rms 0.11 dB
```

Two corrections to the plan's §6.1 anchors: the scoop is far **broader** than
Q ≈ 0.55 (0.27 fits to a tenth of a dB), and **100 Hz is −5.5 dB rel 10 kHz,
not −7.5** — test row S1 should adopt the table above (suggest checking 60 Hz
−1.5, 250 Hz −11.9, 500 Hz −14.5, 1 kHz −11.8, 2 kHz −6.9, 5 kHz −1.7, all
±1 dB). The old "+2.5 dB at 20 Hz" anchor was right (+2.2 measured).

### The MID control law [M], if it is ever exposed

Notch depth rel 10 kHz vs pot position (audio-taper A1k, R134 39k shunt):
99% → −14.5, 88% → −15.2, 66% → −17.1, 55% → −18.2, 44% → −19.6, 33% → −21.1,
23% → −23.1, 12% → −25.8, 1% → −29.6 dB; centre drifts 455 → 552 Hz and the
notch narrows as it deepens. Bass/mid/treble are all cut-only (passive), which
the digitization confirms: nothing ever rises above the 20 kHz reference.

### Caveats that are now on the record [R]

- The curve is the **CP-80 at Brilliance HIGH** (the least-attenuated switch
  position; CP-70 has no brilliance switch and its stack couples straight
  through — HIGH is the right proxy, and the Sullivan calibration set is a
  CP-80 anyway).
- **CP-70B and CP-80 stacks differ in at least one value**: R134 (the mid-pot
  shunt) reads 3.9 kΩ on the CP-70B overall circuit but 39 kΩ on the CP-80
  schematic and its redraw. A true CP-70 preset needs the stack re-solved from
  the CP-70B netlist; for a model calibrated on CP-80 samples the digitized
  curve is self-consistent.
- CP-70B spec: tone controls are *"detented at 'flat' response"* — if the
  detent sits mid-travel rather than full-up, "controls flat" recordings carry
  a deeper scoop than the 99% curve. Unresolvable from documents; the S1 row
  tests the filter, and calibration against the samples absorbs the offset.

### Tremolo [R] — components identified, time constants still inferred

- Optocoupler: **MCD-527 LDR** (TRD101/TRD102), one LED + one CdS cell each,
  two channels driven in antiphase by TR101/TR102 (2SC458C) from one op-amp
  oscillator (IC102 = RC4558; timing network C124 2.2 µF with 39 k/
  100 k + dual-gang SPEED pot VR6 B100k×2; DEPTH VR5 C20k×2; 5.1 V zener
  amplitude clamp).
- Panel spec: SPEED **0.8±0.5 – 10±1 Hz**, DEPTH **>40% max, <15% min** —
  confirming the plan's §6.2 ranges verbatim.
- MCD-527 attack/decay is published nowhere; the community replacement is the
  **NSL-32SR2S** (same-family CdS). The Suitcase photocell constants
  (2.5/35 ms) remain the working stand-in — same physics, same era of cell.
  Closure stays plan open-question 10: A/B against any tremolo-up CP recording.

---

## 7. Windfalls found on the way

**The full 88-key factory stretch table** [R] (SM p. 14, "Tuning the CP-80",
A49 = 440 Hz) — replaces the plan's 10-point interpolation with Yamaha's own
per-key cents. CP-80 key = MIDI − 20:

| key | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c | −23 | −21 | −19 | −17 | −16 | −15 | −14 | −13 | −12 | −11 | −10 | −9 |

| key | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c | −8 | −7 | −7 | −6 | −6 | −5 | −5 | −5 | −4 | −4 | −4 | −4 | −4 |

| key | 26–28 | 29–30 | 31–33 | 34–42 | 43–49 | 50–53 | 54–56 | 57–59 | 60–62 | 63–65 | 66–67 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c | −3 | −3 | −2 | −1 | 0 | +1 | +2 | +3 | +4 | +5 | +6 |

| key | 68 | 69 | 70 | 71 | 72 | 73 | 74 | 75 | 76 | 77 | 78 | 79 | 80 | 81 | 82 | 83 | 84 | 85 | 86 | 87 | 88 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| c | +7 | +7 | +8 | +8 | +9 | +9 | +10 | +11 | +12 | +14 | +16 | +18 | +20 | +21 | +23 | +25 | +27 | +29 | +31 | +33 | +35 |

(Read from a 300 dpi scan; every value is monotone-consistent with its
neighbours. The E1 offset for the CP-70 compass is key 8 = **−13 c**, E7 is
key 80 = **+20 c** — the plan's K3 row endpoints hold exactly.)

**Pickup bus, at schematic precision** [R] (CP-70B overall circuit): 73
elements in **six physical blocks** — keys 1–15, 16–25, 26–37, 38–50, 51–56,
57–73 — joined directly; the only electrical attenuation is **two 4.7 kΩ
mixing resistors inside the treble run, between keys 63|64 and keys 65|66**
(F#6|G6 and G#6|A6, MIDI 90|91 and 92|93). The plan's §5.2 breakpoints are
confirmed; note the attenuated region coincides with the undamped top. The
CP-80's extra 0.022 µF mixing capacitor between keys 82|83 is confirmed on the
SM wiring fold-out.

**Hammer/whippen grading** [R]: hammers in 4 groups (CP-70 keys 1–25 / 26–50 /
51–63 / 64–73), whippens in 3–4 groups — mass graduation on the real
instrument is stepwise.

---

## Status summary

| # | Question | Status | Confidence | Cheapest remaining step |
| --- | --- | --- | --- | --- |
| 1 | Whose length is 679 mm | **Closed**: CP-70 E1 (Lenhoff p. 326 via Wikipedia; case + physics corroborate) | High | — |
| 1b | Wound-bass L/T/μ curve | **Narrowed**: E1 679 mm, T ≈ 450–600 N, μ 140–190 g/m; A0 (CP-80) 800–870 mm; lengths saturate at the case | Med-high | Schaff winding spec request, or tape measure |
| 2 | Strike position β | Open (documents exhaustively lack it); regulation dims recovered | — | Scaled photo of one harp |
| 3 | Piezo capacitance | Open; window 20–50 nF bus stands | Medium | One LCR reading |
| 4 | Top damped note | **Closed**: A6 / MIDI 93 both models, from the damper-head part tables | High | — |
| 4b | Damper release behaviour | Open; felt shapes + 3-grade levers documented | — | Staged-release recording |
| 5 | T60 beyond 30 dB | Open; freesound/archive.org/pianobook exhausted | — | One note to silence, any CP |
| 6 | Mid scoop shape | **Closed**: digitized curve + fit (488 Hz, −13.4 dB, Q 0.27, shelves +4.0/+1.9); 100 Hz anchor corrected to −5.5 | High (CP-80 HIGH) | CP-70B stack re-solve if a true CP-70 preset is wanted |
| 6b | Tremolo LDR constants | Narrowed: MCD-527 identified, NSL-32SR2S equivalent; constants still inferred | Medium | A/B vs a tremolo-up recording |

## Sources

- CP-80 Service Manual (1978.9), pp. 2, 4–8, 14–16, 18–28 + 11×17 fold-outs
  (wiring, TR/BR circuit) — scans on disk, manuals.fdiskc.com
- Electric Grand Parts List CP-70/70B/80 (Apr 1982), pp. 18–20 (damper heads),
  22 (hammer/whippen grades), 23–26 (key assy), 27–28 (frame/strings)
- CP-70B Operating Manual, p. 12 (specifications incl. case dimensions),
  pp. 14–15 (overall circuit: pickup blocks, mixing resistors, tone stack,
  tremolo), p. 16 (block diagram)
- tjscientist/Yamaha-Preamp-Work — SmartPDF redrawn schematic, TINA-TI study,
  `CP-80_Preamp_Frequency_Response_HI_M_99.PNG` (digitized here)
- Wikipedia "Yamaha CP-70", citing Lenhoff & Robertson, *Classic Keys* (2019),
  pp. 326, 334–335, 345
- Bremmer RPT, pianotech, 1 Nov 1998 (Schaff holds the wound-string specs) —
  moypiano.com/ptg/pianotech.php/1998-November/039217.html
- Digitization and window computations: this session's scripts (pixel-grid
  calibration against the plot axes; string solver over B, T, L, OD)
