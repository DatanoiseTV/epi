# Hohner Clavinet D6/E7 — measured

Companion to `transducers-and-chassis.md` §1.3/§5.3, which already carries the
pickup-family analysis. This file is every number the sources give, with units
and provenance, for the fourth instrument. Tags: **[M]** measured by the cited
authors, **[C]** computed here from other numbers, **[R]** read in a primary
document, **[V]** vendor data, **not found** — looked for, is not there.

**Primary sources on disk** (agent scratchpad, session 0408a268):

- `eurasip103.pdf` — Gabrielli, Välimäki, Penttinen, Squartini, Bilbao, "A
  digital waveguide-based approach for Clavinet modeling and synthesis",
  EURASIP J. Adv. Signal Processing 2013:103. Analysis of a real Hohner D6,
  semi-anechoic recordings at 48 kHz, whole compass. Cited below as *EURASIP*.
- `dafx12.txt` / DAFx-12 — Remaggi, Gabrielli, de Paiva, Välimäki, Squartini,
  "A pickup model for the Clavinet", DAFx-12, York 2012. Physical inspection
  and Vizimag field simulation of real D6 pickups. Cited as *DAFx-12*.
- `clavinet_d6.pdf` — Hohner Clavinet D6 General Servicing Instructions,
  Matth. Hohner AG, Trossingen (ER 192 7/76), with full amplifier schematic
  (Schaltbild 710 314). Cited as *D6 manual*.
- `clavinet_e7.pdf` — Hohner Clavinet E7 General Servicing Instructions
  (EB 289 12/80), schematic St.-Nr. 801 111. Cited as *E7 manual*.
- Vintage Vibe replacement string listing (Mapes-wound), for gauges. Cited as
  *VV*.

---

## 1. Instrument geometry

- 60 keys, one string per key, compass F1–E6 (manuals write F–e³) [R, both
  manuals; EURASIP §2].
- First 23 strings wound, the rest plain; the timbre discontinuity between
  keys 23 and 24 sits at ≈150 Hz [M, EURASIP §2.3.2 Fig. 6]. VV's gauge chart
  puts the wound/plain break after E3 (24 wound strings) — one-key
  disagreement, see §11.
- Keyboard-end termination: tuning pin, with a **wool yarn winding** woven
  around the string between pin and anvil; far end: tailpiece on a metal bar
  [R, EURASIP §2; both manuals]. Guitar-style geared tuning pins, each pin one
  octave to the left of its key [R, both manuals]. Strings are "relatively
  slack" [R, both manuals] — no tension figure anywhere, see §11.
- Mute slider ("Lautenzug"): moves a damper bar onto the strings for a "dull,
  dry sound" [R, both manuals].
- One string length anchor: **67.8 cm speaking length at f₀ = 161 Hz**
  [M, DAFx-12 §3.1] → transverse wave speed c = 2Lf₀ = 218 m/s on that string
  [C]. No per-key length table exists in any source on disk.

## 2. String gauges [V]

Vintage Vibe / Mapes replacement set (62 strings incl. two spares; note names
below corrected for octave typos in the listing so the counts add up to 60):

| Gauge (in) | Keys | Count | Wound? |
| --- | --- | --- | --- |
| .032 | F1–D2 | 10 | wound |
| .028 | D#2–G2 | 5 | wound |
| .022 | G#2–E3 | 9 | wound |
| .011 | F3–G3 | 3 | plain |
| .009 | G#3–E6 | 33 | plain |

VV added the .011 tier themselves ("the only … to achieve a more consistent
tone … by including .011 gauge strings for keys F3–G3"), so the original
Hohner set likely ran .009 from F3 up; treat the .011 row as VV, not Hohner.
Note the F3 gap: nothing covers E3→F3 wound→plain except the break itself.

## 3. Inharmonicity [M, EURASIP §2.3.2, Table 1]

B estimated from partials 2–7 (N = 6, loudness-weighted Bₙ average) at eight
keys, linear interpolation elsewhere. Design values (Table 1, with the
dispersion-filter design bandwidth and β they used):

| Key | B | BW (Hz) | β |
| --- | --- | --- | --- |
| F1 | 5·10⁻⁴ | 436.5 | 0.85 |
| A1 | 2·10⁻⁴ | 582.7 | 0.85 |
| D3 | 9·10⁻⁵ | 1468.3 | 0.85 |
| D3 | 1·10⁻⁴ | 1555.6 | 0.85 |
| F5 | 9·10⁻⁵ | 7399.9 | 0.85 |
| E6 | 8·10⁻⁵ | 13185.1 | 0.85 |

(The duplicate "D3" row is printed that way in the paper — the two rows
bracket the wound/plain break, so the second is almost certainly D#3 or E3.)
Low-range B exceeds the Järveläinen audibility threshold — clearly audible;
it crosses under the threshold for high notes [M, Fig. 6]. Compare CP-70:
B here is one to two orders smaller than the CP bass (1.22·10⁻³ at MIDI 27) —
a much less dispersive string.

## 4. Tangent and action [R/M]

- Class-2 lever; the key presses a rubber tip ("tangent", manuals: plunger /
  Stößel, orig. hammer tip) that strikes the string and **traps it against a
  metal anvil (stud) for the whole note**, splitting the string into speaking
  and non-speaking parts [R, EURASIP §2; both manuals].
- Key/tangent velocity range **1–4 m/s**, mapped linearly to MIDI 1–127
  [M/R, EURASIP §3.2].
- Strike strength sets both level and overtone content ("a heavier touch
  enhances the proportion of overtones") [R, E7 manual].
- The player's finger stays mechanically coupled to the string through the
  key while held ("direct finger contact with the string through the key and
  the plunger; the note continues to sound as long as the connection is
  maintained") [R, D6 manual, Intonation].
- Attack waveform (pickup signal = time derivative of displacement): low/mid
  tones show a clear smooth positive pulse plus reflections; the excitation
  pulse is a smooth triangle [M, EURASIP §2.3.1, Fig. 4]. Their fitted pulse:
  order-6 polynomial, coefficients (descending) −2.69·10⁻⁸, 2.53·10⁻⁶,
  −9.54·10⁻⁵, 1.74·10⁻³, −1.44·10⁻², 4.50·10⁻², −3.50·10⁻²; length
  N = fs·d/v samples for tangent–string distance d [M, EURASIP §3.2, Eq. 6–7].
- Tangent knock: the tangent/anvil impact rings the soundboard into the
  pickups; energy concentrated **below ≈1200 Hz**, clearly audible only for
  high notes (E6 f₀ > 1300 Hz) [M, EURASIP §3.5].

## 5. Release and yarn damper [M, EURASIP §2.3.1]

- On release the tangent leaves the anvil, speaking and non-speaking parts
  reunite, and the yarn damper kills the vibration. The re-united string
  produces a **pitch drop of three semitones, constant across the whole
  keyboard** ("given the geometry of the instrument"), of short duration,
  visible in the release spectrogram (Fig. 5).
- Three semitones ⇒ total length / speaking length = 2^(3/12) = 1.189; the
  non-speaking (yarn-wrapped) segment is 18.9 % of the speaking length,
  15.9 % of the total [C].
- Release is short "at least with an instrument in mint condition, with an
  effective yarn damper" [M] — damper efficacy is the aging variable (the
  wool compresses; VV sells a gel replacement).
- Yarn spec, for what it is worth: the community FAQ names Caron "Aunt
  Lydia's Craft & Rug Yarn" as the replacement wool [R, clavfaq p. 10].

## 6. Decay, beating, stability [M, EURASIP §2.3]

- Sustain T60 **up to 20 s or more** for low/mid tones; shorter for high
  tones. Minimal energy transfer to the body (the keybed is a support, not a
  radiator — the acoustic output is feeble).
- Per-partial T60 decreases with partial number; the lowest 2–4 partials ring
  markedly longest. The T60-vs-partial envelope is not monotone but **ripply,
  with periodicity 2–3 × f₀** (Fig. 8, E4).
- Their ripple-filter match to that: Rrate ∈ [1/3, 1/2], r ∈ [−0.006, −0.001],
  randomized per keystroke; the Fig. 8 fit uses Rrate = 1/2, r = −0.006
  [M, §3.1].
- Partial beating: occurs only for keys **up to E4**, amplitude up to
  **15 dB peak-to-peak at 0.5–2 Hz**, intermittent, slight correlation with
  key velocity, mechanism "still not understood"; present in both microphone
  and pickup recordings, so it is acoustic, not electrical [M, §2.3.4].
- f₀ during sustain is stable to **1–2 cents** — below audibility [M, §2.3.3].

## 7. Spectrum [M, EURASIP §2.3.4]

- The **second partial always sits > 3 dB above the fundamental**; often the
  third exceeds the second (Fig. 3b, representative across the compass).
- Pickup comb pattern: **notches at every 5th partial** for the A2 tone,
  "at approximately 544 Hz and multiple frequencies" (printed f₀ 116.5 Hz;
  544/116.5 = 4.67, so the readout point sits at d/L ≈ 0.21 from the
  termination for that key [C]).
- Printed reference pitches: "A2" 116.5 Hz and "D2" 77.78 Hz are a semitone
  above standard (A#2/D#2 values), while D3 146.8 Hz, A4 440.0 Hz and E4 are
  standard — take the paper's note names with one-semitone salt.

## 8. Pickups [M, DAFx-12; EURASIP §2.3.5]

Two electrically identical epoxy-potted single-coil bar pickups.

- Construction: **six metal bar coils per pickup, ten strings per bar**; each
  bar 0.5 cm wide, 3.7 cm long (center pickup) / 3.3 cm long (bridge pickup)
  [M, DAFx-12 §2]. (EURASIP §2.1 says "10 metal bar coils … six strings
  each" — the dedicated pickup paper's inspection wins; see §11.) Magnet
  thickness and winding count unmeasurable (epoxy).
- Position: **center pickup 18.5 cm (lowest string) → 6.5 cm (highest) from
  the string termination; bridge pickup a constant 4 cm**, tilted ≈30°
  relative to the center pickup [M, DAFx-12 §2]. (Which one sits above vs
  below the strings is stated oppositely by the two papers; see §11.)
- Position comb: H(z) = 1 − βz^(−2N), β = 1 for an ideal inverting
  termination; for the 67.8 cm / 161 Hz string N = 200 samples (4.2 ms) for
  the center pickup, N = 84 (1.8 ms) for the bridge [M, DAFx-12 §3.1].
  (These N values are not mutually consistent with c = 218 m/s and the
  measured distances — see §11; the distances and the every-5th-partial
  observation are the trustworthy anchors.)
- Flux vs vertical displacement (Vizimag, 0–20 mm in 21 steps): negative
  exponential, ≈0.09 T at contact falling to ≈0.07 T at 2 cm; published
  4th-order polynomial fit p₀…p₄ = 0.7951, −1.544, 1.818·10², −9.508·10³,
  1.817·10⁵ [M, DAFx-12 Table 2 = EURASIP Table 2].
- Along-bar (horizontal) sensitivity negligible: E_vert/E_horiz = **25 dB**
  (string at bar center) to **30 dB** (string near bar edge) for 1 mm p-p
  motion, which is the maximum observed string oscillation [M, DAFx-12 §3.2].
- Electrical impedance measured: frequency response (∝ 1/Z) flat within
  **< 1 dB**; cable capacitance negligible (short shielded run + PCB traces)
  [M, EURASIP §2.3.5, Fig. 9]. The pickup output is the time derivative of
  flux (Faraday), i.e. a perfect differentiator on displacement [R].
- The 4-position switch matrix [R, DAFx-12 Table 1; till.com; both manuals]:

| Switch 1 | Switch 2 | Result | Timbre (as described) |
| --- | --- | --- | --- |
| A | C | center pickup only | warm |
| B | C | bridge pickup only | bright, brash |
| B | D | sum, in phase | full, deep |
| A | D | sum, anti-phase | thin — fundamental damped |

## 9. Preamp and tone filters [R, D6/E7 schematics; M, EURASIP §2.3.6]

Signal chain (both models): pickups → A–B/C–D switch matrix → step-up
transformer **1:15** (Beyer TR 145 / BV 35 569) → first stage BC 550C →
switched tone network → second stage (D6: BC 148B; E7: BC 550C) → volume pot
100 kΩ (D6 linear, E7 log) → output (E7 adds a BC 238 emitter follower and a
ZPD 8.2 (8.2 V) zener supply regulator). Supply: 9 V battery IEC 6F22; D6
external adapter jack "6 V only", E7 accepts 6–20 V. D6 output socket is
labelled "Output 100 mV" — the nominal full output level [R].

The four tone rockers insert first/second-order passive branches (schematic
labels tief/mittel/hoch/scharf = the D6's soft/medium/treble/brilliant). At
least one must be down or the instrument is silent [R, both manuals].
Component values, identical in both schematics and in EURASIP Table 3:

| Rocker | Components | Z_i(s) | Order |
| --- | --- | --- | --- |
| Soft (tief) | R = 30 kΩ, C = 0.1 µF | R/(1+sRC) | 1st |
| Medium (mittel) | R = 10 kΩ, C = 15 nF | R/(1+sRC) | 1st |
| Treble (hoch) | L = 2 H, C = 4.7 nF | sL/(1+s²LC) | 2nd |
| Brilliant (scharf) | L = 0.6 H | sL | 1st |

Inductor construction (printed on both schematics): 2 H = 700 turns 0.09 mm
CuL, 0.6 H = 400 turns 0.1 mm CuL, on Siemens 14×8 pot cores, A_L 4200, N30
material. EURASIP digitizes each Z_i by bilinear transform and cascades the
active H_i(z) (their Table 3; Fig. 14 shows the SPICE-vs-IIR match for
medium‖treble‖brilliant). Their SPICE result for the amplifier with the tone
stack removed: near flat with a mild **−3 dB low shelf at 130 Hz** and a
**+3 dB high shelf at 4 kHz** [M, §2.3.6].

Nonlinearity, measured on the real amplifier (1 kHz sine, signal analyzer):
**THD 1 % at 400 mV input** (the maximum pickup level in normal polyphonic
playing), rising to **3.6 %** at fortissimo chord peaks; they neglect it as
masked [M, EURASIP §3.4]. Unshielded single coils + transistor amp = audible
mains/EMI noise floor as a documented character trait [R, both papers].

## 10. Reference model numbers (EURASIP's own DWG, for cross-checks)

Not measurements of the instrument, but the published model that passed
listening tests (discrimination 53 % average, 58 % expert, vs 50 % = chance):

- fs 44.1 kHz; longest delay line 923 samples (F1), ≈1000 samples/string with
  comb taps; dispersion filter max order 8 (4 SOS) [R, §3.6/§4].
- Ripple filter r, Rrate as in §6 above; beating equalizers: ≤3 audible at
  once, gain law K[n] = 10^(|cos(2πfn)|/20) [R, §3.1].
- Comb gain −1; comb delay from measured pickup distance ratio × loop delay;
  the dispersion duplicate in the comb path is *omitted* for cost (+25 %) —
  a known accuracy sacrifice their model makes and ours need not [R, §3.3].
- Knock: one sample extracted from an E6 tone, lowpassed with slightly
  randomized cutoff per trigger [R, §3.5].

## 11. Contradictions and gaps, explicit

1. **Coil count**: DAFx-12 (dedicated inspection): 6 bars × 10 strings.
   EURASIP: 10 bars × 6 strings. Believe DAFx-12; either way the bar is wide
   against the string spacing and flux is flat along it.
2. **Which pickup is above the strings**: EURASIP §2.1 says bridge above
   (tilted 30°), center below; DAFx-12 §2 says center above, bridge below.
   Same authors, one year apart. Irrelevant to the signal model (vertical
   axis only); record and move on.
3. **Comb N vs distance**: N = 200/84 at 48 kHz implies one-way distances of
   ~0.9/0.4 m on a 0.678 m string — inconsistent with the measured 18.5/4 cm.
   The distances, the 4.2/1.8 ms figures, and every-5th-partial are mutually
   inconsistent as printed; anchor on the physical distances and the notch
   observation, not on N.
4. **Wound/plain break**: EURASIP 23 wound; VV gauge chart implies 24 (ends
   at E3). One key, at the ~150 Hz discontinuity either way.
5. **Not found anywhere**: per-key speaking lengths (one anchor: 67.8 cm @
   161 Hz), string tensions (only "relatively slack"), tangent rubber
   stiffness/damping, anvil geometry, yarn damping coefficient, pickup
   winding count/inductance, transformer core data beyond type number.
6. **Tuning of the analyzed D6**: two printed tone pitches sit a semitone
   high of their note names (§7) — do not calibrate absolute pitch mapping
   against the paper's note names without checking the stated Hz.
