# Keyboard velocity dynamics — measured

What the measured literature says about how key velocity becomes hammer
velocity, level, and spectrum, and what that implies for Epi's
MIDI-velocity-to-strike maps. Tags: **[M]** measured by the cited authors,
**[R]** read in a document during this session, **[C]** computed here from
other numbers, **[T]** training knowledge pending verification (the fetch
failed or the primary is paywalled), **[D]** design decision for Epi,
**not found** — looked for, is not there.

**Sources actually fetched this session:**

- Askenfelt & Jansson, "From touch to string vibrations", in *Five Lectures on
  the Acoustics of the Piano* (Royal Swedish Academy of Music, 1990), HTML
  edition at speech.kth.se/music/5_lectures/ — the `motions` and `timing`
  chapters were fetched and read. Cited as *5L*.
- Dannenberg, "The Interpretation of MIDI Velocity", Proc. ICMC 2006,
  pp. 193–196 — full PDF fetched from cs.cmu.edu and read. Cited as *D06*.
- OpenAlex metadata + abstracts (fetched): Askenfelt & Jansson JASA I
  (doi 10.1121/1.399933, JASA 88(1):52–63, 1990) and II (doi 10.1121/1.402043,
  JASA 90(5):2383–2393, 1991); Goebl, Bresin, Galembo, "Touch and temporal
  behavior of grand piano actions", JASA 118(2):1154–1165, 2005
  (doi 10.1121/1.1944648, abstract only, closed access); Goebl, Bresin,
  Galembo, "The piano action as the performer's interface", SMAC 2003 / OFAI
  TR-2003-15 (abstract only — the OFAI PDF host `cis.ofai.at` no longer
  resolves).
- RebelTechnology/OwlProgram `LibSource/VelocityCurve.h` (via `gh search
  code`) — quotes the GM DLS Level 1 concave transform verbatim.
- This repo's own measured docs: `wurlitzer-200a.md`, `clavinet-measured.md`,
  `cp70-measured.md`, `rhodes.md`.

**Fetches that failed:** the STL-QPSR archive paths and the KTH TMH
publications database (`speech.kth.se/prod/publications/...`) return HTTP 500;
PubMed and web.archive.org are blocked in this environment; Semantic Scholar
rate-limited; the MMA DLS/GM2 specification itself is behind the midi.org
login. Everything that leans on those is tagged [T].

---

## 1. Key velocity to hammer velocity on real grand pianos

### Measured magnitudes

- Key velocity at **mezzo-forte: max ≈ 0.3–0.5 m/s**; at **forte the key peak
  seldom exceeds 1 m/s** [M, 5L motions].
- The hammer travels **≈ 5× the key's distance in essentially the same time**,
  so hammer velocity is **≈ 5× key velocity** (measured at C4, mf/f)
  [M, 5L motions].
- **Maximum hammer velocity ≈ 5 m/s at forte** [M, 5L motions]. Other work
  puts the extreme ff/staccato top at 6–7 m/s [T — Askenfelt & Jansson II and
  the Chaigne & Askenfelt simulation papers; not re-read this session].
- Extrapolating the ×5 lever ratio down: pp key motion of 0.05–0.1 m/s gives
  **pp hammer velocities ≈ 0.25–0.5 m/s** [C from the two 5L numbers above] —
  consistent with the 0.2–0.5 m/s pp figures usually quoted from Goebl et
  al. [T].
- Geometry: hammer blow distance (crown to string) **45–47 mm**, let-off
  **1–3 mm** [R, 5L timing]. Key dip ≈ 10 mm against 46 mm of hammer travel
  reproduces the ×5 ratio [C].
- The hammer is not rigid in flight: a **≈ 50 Hz** "backwash" flex plus a
  **≈ 400 Hz** shank ripple ride on the velocity [M, 5L motions].
- Full pp→ff hammer-velocity span is therefore roughly **×10–×30**
  (0.2–0.5 m/s up to 5–6 m/s), i.e. 20–30 dB if radiated pressure is taken
  first-order proportional to hammer velocity [C; the felt nonlinearity puts
  part of the growth into the spectrum instead, §3].

### Touch: pressed vs struck

- Goebl, Bresin & Galembo (JASA 2005): 2300 tones, five keys, several grands,
  played **"pressed"** (finger on key) vs **"struck"** (finger arriving with
  speed) over the whole dynamic range. **Travel time from finger–key contact
  to hammer–string contact, as a function of final hammer velocity, differs
  clearly between the two touch types but only slightly between piano makes**
  [R, fetched abstract].
- Travel times run from **≈ 25 ms at ff down and ≈ 160–200 ms at pp** [T —
  numbers are in the paper body, which is closed-access; the abstract confirms
  only the design and the touch-type effect].
- The SMAC 2003 companion found **"no effect of touch type … on peak sound
  level"** [R, fetched OpenAlex abstract of OFAI TR-2003-15]: at equal final
  hammer velocity the tone level is the same; what touch changes is the
  *noise* component (finger–key thump, key-bottom knock) and the timing
  feel. Askenfelt & Jansson report the same division: the tonal part of the
  note is set by hammer velocity at let-off, touch colours the attack noises
  [T for the exact wording; consistent with 5L].
- Consequence for a synth: **a single scalar per note (hammer velocity) is the
  correct tonal axis**; pressed/struck belongs in the action-noise layer, not
  in the strike map. Epi already separates these (`ActionNoise.h`) [R, this
  repo].

### Bibliography anchors [R, OpenAlex]

- Askenfelt & Jansson, "From touch to string vibrations. I: Timing in the
  grand piano action", JASA 88(1):52–63, 1990.
- Askenfelt & Jansson, "… II: The motion of the key and hammer", JASA
  90(5):2383–2393, 1991 (optical position measurement, dynamics and touch
  types, let-off force, free flight, hammer modal analysis).
- Goebl, Bresin, Galembo, "Touch and temporal behavior of grand piano
  actions", JASA 118(2):1154–1165, 2005.

## 2. MIDI velocity semantics

### What the spec says

- **The MIDI 1.0 specification does not define how velocity maps to level.**
  D06 states it plainly: "the MIDI standard (MMA 1996) does not specify
  exactly how velocity should be interpreted" [R, D06 §1]. The spec's only
  velocity semantics: 1–127, 0 = note off, 64 = default for non-sensing
  keyboards; the often-reproduced "logarithmic" curve figure is informative,
  not normative [T — spec itself not fetchable].
- **GM DLS Level 1 / GM2 renderers**: the note velocity is converted to
  attenuation by the Concave Transform, **attenuation = 20·log10(127²/v²) dB,
  i.e. gain = v²/127², i.e. 40·log10(v/127) dB** [R, quoted verbatim from the
  DLS L1 spec in OwlProgram `VelocityCurve.h`; [T] against the MMA text
  itself]. Full span vel 1→127 under this law: **40·log10(127) = 84.2 dB**
  [C]. The SoundFont 2 default velocity-to-initial-attenuation modulator is
  the same concave shape scaled to 960 cB = 96 dB [T].

### What commercial synths actually do — Dannenberg's measurements [M, D06]

Method: 15 velocities × 128 programs per device, peak short-term RMS (1050-
sample windows at 44.1 kHz), normalised at vel 100. Findings:

- **Square law fits, log does not**: peak RMS ≈ (m·v + b)² is near-exact for
  the software synths; a straight line in dB (exponential) is a poor fit
  [M, D06 §5]. Only the Kurzweil K2000R behaved log-like.
- A square law normalised at the top is fully characterised by its **dynamic
  range r_dB from vel 1 to 127**: r = 10^(r_dB/20), b = 127/(126·√r) − 1/126,
  m = (1 − b)/127 [R, D06 eqs. 9–11].
- Measured r_dB, averaged over all programs (piano program alone in
  parentheses): Roland Sound Canvas **89 (45)**, Microsoft GS **61 (81)**,
  Yamaha SY22 **21 (18)**, Yamaha DX7 **11 (15)**, Roland U220 **20 (51)**,
  Kurzweil K2000R **25 (37)**, Garritan Personal Orchestra **44 (105)** dB
  [M, D06 Table 1]. Spread across devices: **> 60 dB** — there is no de facto
  standard. Hardware stage instruments sit far narrower (11–25 dB) than
  software renderers.
- Dannenberg's recommendation: square law with **r_dB = 60 dB** [R, D06 §8].
  "Values from 20 to 60 dB seem to be typical" [R, D06 §7].
- Keyboard **controllers** additionally apply their own selectable curve
  families (soft/normal/hard, plus fixed) before the synth ever sees the
  number, so velocity data is doubly non-standard [T — vendor manuals, none
  fetched].

## 3. Dynamic range and spectrum of the real instruments

- Single acoustic piano note, pp→ff: **≈ 20–35 dB** peak-level span; the whole
  instrument (bass ff vs treble pp) covers more, ≈ 60 dB SPL at the player's
  ear from ~50–60 dB(A) to near 100 dB [T — Meyer, and Askenfelt's own
  summaries; not fetchable this session].
- That span is consistent with the measured hammer-velocity ratio: ×23–×32
  → 27–30 dB with pressure ∝ hammer velocity [C].
- The growth is not spectrally flat: the felt hammer stiffens with
  compression, so **higher partials grow faster than the fundamental** and
  the hammer–string contact time shortens (≈ 4 ms pp to ≈ 1–2 ms ff,
  mid-range) [T — Askenfelt & Jansson III (JASA 93(4):2181–2196, 1993) and
  Hall's 5L chapter; the fetched 5L pages carry only the qualitative
  statement].
- Perceptual studies summarise this as **brightness (spectral centroid)
  growing roughly linearly in log hammer velocity** [T — no fetchable primary
  found this session; OpenAlex searches returned nothing citable with
  numbers]. Treat the direction (monotone centroid growth) as solid and the
  functional form as unverified.
- An electromechanical measured anchor from this project: on the reed piano,
  energy in harmonics 2–12 relative to the fundamental grows **≈ 32 dB
  pp→ff at A1**, falling with register until the top does not bark at all
  [M, this repo `wurlitzer-200a.md` "Velocity and register"]. The spectral
  side of the dynamic, not the level side, carries most of the expression.

## 4. Electromechanical keyboards

- **Clavinet**: tangent velocity **1–4 m/s, mapped linearly to MIDI 1–127**
  in the only published model of a measured D6 [M, Gabrielli et al., EURASIP
  J. Adv. Sig. Proc. 2013:103 §3.2, via `clavinet-measured.md`]. That is a
  **12 dB** velocity span [C] — the narrowest of the five instruments, which
  matches the instrument's percussive, compressed feel.
- **Reed piano**: service manual action geometry — hammer blow distance
  **30.95 mm**, let-off **3.18 mm**, key dip **9.53 mm** [R, service manual
  pp. 16–19 via `wurlitzer-200a.md`], so ≈ 27.8 mm of driven travel and a
  free flight of 3.18 mm [C]. No published key-to-hammer velocity
  measurement exists — **not found**. The measured harmonic-growth table
  (§3) is the instrument's documented dynamic response.
- **Tine piano**: the service manual specifies escapement from **1/4–3/8 in
  in the bass falling to 1/32 in in the treble** (long bass tines whip far
  enough to be struck twice by a closer hammer) [R, service manual via the
  `RhodesVoice.h` strike comment]. Players' folklore — "narrow but
  nonlinear", the bark arriving only near the top — is documented nowhere as
  a measurement; the physical account (pickup-gap nonlinearity taking over
  from level growth) is the model's own [R, this repo `rhodes.md`,
  `transducers-and-chassis.md`]. **No measured key-velocity-to-tine-velocity
  relation exists in any source found.**
- **Electric grand**: the reference sample set has only 4 velocity layers and
  no recorded hammer velocities [R, `cp70-measured.md`]; action is a real
  (shortened) grand action, so §1 numbers carry over [T].

## 5. What Epi does today [R, this repo]

- Engine boundary: JUCE float velocity = MIDI/127, linear, u ∈ [0,1].
- User stage (`EpiEngine.cpp:342–347`): `vel = velMapEval(u)^shape ·
  expression`, with `shape = 0.35 + 1.9·velCurve`, and `velMapEval` a
  **five-point monotone Fritsch–Carlson cubic over fixed abscissae
  {0, 1/4, 1/2, 3/4, 1}**, identity by default (`EpiEngine.h:236–253,
  395–420`). The 5-point editor the design asks for already exists.
- Physics stage, launch speed in m/s:
  - Grand (`GrandVoice.h:244`): v = **0.25 + 5.5·vel^1.7** → 0.25–5.75 m/s,
    span ×23.0 = **27.2 dB** [C].
  - Tine / reed / e-grand (`RhodesVoice.h:328`, `WurliVoice.h:408`,
    `CP70Voice.h:217`): v = **0.18 + 5.6·vel^1.7** → 0.18–5.78 m/s, span
    ×32.1 = **30.1 dB** [C].
  - Clav (`ClavinetVoice.h:324`): v = **1 + 3·vel** → 1–4 m/s, **12.0 dB**
    [C] — exactly the EURASIP measured law.

### Does the research support vel^1.7?

Yes, as a system. The composite requirement is: endpoints inside the measured
hammer-velocity band (they are: 0.18–0.25 m/s floors, 5.75–5.78 m/s tops,
§1), and a level-vs-MIDI-velocity curve near the industry square law with a
*piano-like* span. Check [C]: a Dannenberg square law with r_dB = 30 dB
(the measured single-note piano span, §3) targeted through the grand's own
law — solve 0.25 + 5.5·w^1.7 = 5.75·(0.8287u + 0.1713)² for w at the editor
abscissae — gives

| u (MIDI/127) | 0 | 0.25 | 0.50 | 0.75 | 1 |
| --- | --- | --- | --- | --- | --- |
| required w | 0 | 0.265 | 0.505 | 0.749 | 1 |

i.e. **the identity map already realises the measured-piano response to
within 0.015 in the ordinate (≤ 0.6 dB over vel 32–127, 2.8 dB at vel 1)**
[C]. A pure exponent-1 law would put mid velocities ≈ 4 dB hotter than the
square-law consensus; exponent 2 was tried and measured too steep
(`RhodesVoice.h:322–327` comment). 1.7 with an additive floor is the right
shape; do not change it.

## 6. [D] Recommendations for Epi

### (a) Default MIDI-velocity-to-strike maps — keep, per instrument

- **Grand, e-grand, tine, reed**: keep `a + b·vel^1.7` with the current
  coefficients and the **identity 5-point map as the default** — §5 shows the
  identity + 1.7 composite *is* the measured-piano curve (square law,
  27–30 dB). The tine/reed instruments deliberately get their extra dynamics
  from the transducer nonlinearity (measured +32 dB harmonic growth, §3),
  not from a wider launch-speed span.
- **Clav**: keep the measured linear 1–4 m/s law and the identity map. Its
  narrow 12 dB launch span is the documented behaviour; widening it in the
  velocity map would falsify the instrument.

### (b) The 5-point curve editor — anchor presets

Ordinates y over the existing fixed abscissae {0, 1/4, 1/2, 3/4, 1}, fed to
the existing Fritsch–Carlson evaluator, `velCurve` (shape) left at its
linear point:

| Preset | y0 | y1 | y2 | y3 | y4 | Intent |
| --- | --- | --- | --- | --- | --- | --- |
| linear | 0 | 0.25 | 0.50 | 0.75 | 1 | identity (bit-exact short-circuit) |
| concert | 0 | 0.265 | 0.505 | 0.749 | 1 | the §5 measured-piano solve; audibly ≡ linear, kept as the honest reference [C] |
| light | 0 | 0.40 | 0.62 | 0.82 | 1 | more tone for a light touch; ≈ +3 dB at mid velocities [C] |
| heavy | 0 | 0.12 | 0.32 | 0.62 | 1 | the player works for the top |
| stage-keyboard | 0.30 | 0.48 | 0.66 | 0.84 | 1 | narrow-range hardware feel: floor at 0.96 m/s compresses the grand span to 15.5 dB (tine 16.1 dB) [C], the DX7/SY22 class (11–21 dB measured, §2) |

Raising y0 (stage-keyboard) is the one preset that needs the editor rather
than the shape knob: it moves the *floor*, which no power law can.

### (c) Verification rows

1. **Monotonicity**: sweep MIDI 1..127 through every preset ×
   `velCurve ∈ {0, 0.5, 1}`; assert the strike scalar is non-decreasing and
   endpoints hit y0^shape and 1. (Fritsch–Carlson guarantees it by
   construction; the row guards the setter's sort/clamp path.)
2. **Endpoint level span**: render one mid-compass note per instrument at
   vel 1 and vel 127 (identity map), measure peak short-term RMS with D06's
   window (1050 samples at 44.1 kHz); assert the span is 27 ± 4 dB for
   grand/e-grand, 30 ± 4 dB for tine/reed (level + early spectrum), 12 ± 3 dB
   for clav — the §3/§4 measured targets. Assert stage-keyboard compresses
   the grand span to 15 ± 3 dB.
3. **Spectral centroid growth**: same notes at vel ∈ {32, 64, 96, 127};
   assert centroid (0–8 kHz, first 500 ms) is strictly increasing in
   velocity for every instrument, and for the reed piano assert harmonic
   2–12 energy re fundamental grows by > 15 dB from vel 32 to 127 on a bass
   note (the measured table shows ≈ 32 dB pp→ff at A1, §3).
4. **Square-law fit**: regress √(peak RMS) on velocity over 15 equally
   spaced velocities (D06's method); assert R² > 0.98 for the identity map
   on the grand, i.e. the shipped default stays in the family Dannenberg
   found in every compliant renderer.

## 7. Contradictions and open questions

- **"Linear-in-dB, 20–40 dB" is a misquote of Dannenberg.** D06 measured the
  opposite of linear-in-dB: a square law fits, a log line does not (K2000R
  excepted), and the spans run 11–89 dB averaged (15–105 dB for piano
  programs) with 20–60 dB called "typical". Anything in this project quoting
  20–40 dB log-linear should cite this file instead.
- **`EpiEngine.cpp:342` comment vs code**: the comment says velCurve = 0.5 is
  linear, but shape(0.5) = 0.35 + 1.9·0.5 = **1.30**; the linear point is
  velCurve ≈ 0.342 [C]. Either the comment or the constants are wrong —
  needs a decision (default-DAW-value 0.5 currently applies a hidden ^1.3 on
  top of the physics law, making the effective mid-velocity response ≈ 3 dB
  quieter than §5 assumes). Not fixed here (file fence).
- **Top hammer velocity**: 5L says ≈ 5 m/s at forte [M]; 6–7 m/s ff figures
  are [T]. Epi's 5.75–5.78 m/s tops sit inside the plausible band either
  way.
- **Goebl 2005 body numbers** (travel times 25–200 ms, hammer floors near
  0.15–0.2 m/s) remain [T]: the paper is closed-access and the OFAI mirror's
  host is dead. Worth one library lookup before citing them in UI copy.
- **DLS concave transform** verified only through a spec-quoting secondary
  source; the MMA text itself is login-walled. Low risk — the formula is
  also what GM2 renderers measurably do (MS GS piano: 81 dB ≈ the 84 dB the
  formula predicts) [C].
- **Centroid ∝ log velocity** has the right sign everywhere but no fetchable
  measured primary was found; the verification row therefore asserts
  monotonicity, not the functional form.
- **No measured key-to-hammer law exists for the tine or reed instruments** —
  the escapement geometry and the project's own spectral table are the only
  documented anchors. If a service-manual-instrumented measurement ever
  surfaces, revisit the 0.18 + 5.6·vel^1.7 coefficients.
