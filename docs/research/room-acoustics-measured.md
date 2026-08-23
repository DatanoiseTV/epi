# Room acoustics — derived profiles

Working notes for the Room profiles in `src/epi/dsp/Room.h`. Project ethos:
everything is computed from physics with published measured anchors, never
sampled impulse responses. Every number the code uses is derived on this page;
the engine-suite rows (`sectionRoom` in `tests/test_epi_engine.cpp`) cite the
tables below. Values that could not be verified by fetch this session are
marked **[T]** (from training, standard handbook figures; verify against the
named source before quoting elsewhere).

## Sources

1. **Absorption coefficients per octave band** — published chart at
   acoustic-supplies.com/absorption-coefficient-chart (fetched 2026-08-23).
   The rows used here match the standard tables reprinted in Everest,
   *Master Handbook of Acoustics*, appendix (absorption coefficients of
   general building materials), and Kuttruff, *Room Acoustics*, material
   tables. A second fetch (Wikipedia, Absorption (acoustics)) cross-checked
   brick, marble, wood floor and heavy-curtain rows to within ±0.02.
2. **Eyring reverberation formula** — Kuttruff, *Room Acoustics*, ch. 5:
   `T60 = 0.161 V / (−S ln(1−ᾱ) + 4mV)` with V in m³, S in m², ᾱ the
   area-weighted mean absorption coefficient, m the air intensity attenuation
   coefficient in 1/m. Preferred over Sabine because the booth's ᾱ ≈ 0.53 at
   500 Hz is far outside Sabine's small-absorption assumption.
3. **Air attenuation** m at 20 °C, 50 % RH, from the ISO 9613-1 attenuation
   figures (dB/km ÷ 4343 → 1/m): **[T]**

   | Hz | 125 | 250 | 500 | 1k | 2k | 4k |
   | --- | --- | --- | --- | --- | --- | --- |
   | m (1/m) | 0.0001 | 0.0003 | 0.00065 | 0.00115 | 0.00207 | 0.00527 |

4. **Image-source model** — Allen & Berkley (1979) shoebox image method,
   truncated to reflection order ≤ 2. Speed of sound c = 343 m/s (20 °C).

## Material table used (α per octave band, 125 Hz – 4 kHz)

| material | 125 | 250 | 500 | 1k | 2k | 4k | source row |
| --- | --- | --- | --- | --- | --- | --- | --- |
| heavy drapery (18 oz/yd², pleated) | 0.14 | 0.35 | 0.53 | 0.75 | 0.70 | 0.60 | fetched |
| carpet on hard floor | 0.01 | 0.02 | 0.06 | 0.15 | 0.25 | 0.45 | fetched |
| plywood panel (5 mm over 50 mm air) | 0.38 | 0.24 | 0.17 | 0.10 | 0.08 | 0.05 | fetched |
| wood flooring on joists | 0.15 | 0.11 | 0.10 | 0.07 | 0.06 | 0.07 | fetched |
| plaster on masonry | 0.01 | 0.02 | 0.02 | 0.03 | 0.04 | 0.05 | fetched |
| natural brick (proxy for coursed rough stone) | 0.03 | 0.03 | 0.03 | 0.04 | 0.05 | 0.07 | fetched |
| marble / dressed stone | 0.01 | 0.01 | 0.01 | 0.01 | 0.02 | 0.02 | fetched |
| plate glass 6 mm | 0.18 | 0.06 | 0.04 | 0.03 | 0.02 | 0.02 | fetched |
| plasterboard 12 mm on studs | 0.29 | 0.10 | 0.06 | 0.05 | 0.04 | 0.04 | fetched |
| occupied upholstered seats | 0.60 | 0.74 | 0.88 | 0.96 | 0.93 | 0.85 | fetched |
| fiberglass board 75 mm | 0.53 | 0.99 | 0.99 | 0.99 | 0.99 | 0.99 | fetched |

## Profile geometry and surface assignment

All rooms are shoeboxes Lx × Ly × Lz. The instrument (source) stands near one
end; the listener sits a few metres out with a small lateral offset so the
early pattern is not perfectly symmetric. Positions in metres.

| # | profile | Lx×Ly×Lz | V (m³) | S (m²) | source | listener | direct r (m) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Booth | 2.0×1.5×2.2 | 6.6 | 21.4 | (0.5, 0.75, 1.1) | (1.4, 0.85, 1.2) | 0.911 |
| 2 | Studio | 6×5×3 | 90 | 126 | (1.2, 2.5, 1.0) | (3.7, 2.8, 1.4) | 2.550 |
| 3 | Stage | 12×9×6 | 648 | 468 | (3.0, 4.5, 1.0) | (7.0, 4.0, 1.6) | 4.076 |
| 4 | Hall | 30×20×14 | 8400 | 2600 | (4.0, 10.0, 1.5) | (12.0, 9.0, 1.6) | 8.063 |
| 5 | Church | 40×18×20 | 14400 | 3760 | (8.0, 9.0, 1.2) | (16.0, 8.0, 1.6) | 8.072 |

Surface mixes (area fractions of each wall):

- **Booth** — vocal-booth build: all four walls heavy drapery, carpet floor,
  fiberglass ceiling.
- **Studio** (live room) — walls 65 % plasterboard + 20 % drapery + 15 %
  glass (control-room window), wood floor, ceiling 60 % fiberglass +
  40 % plasterboard.
- **Stage** (wooden concert platform) — walls 80 % plywood panelling + 20 %
  stage drapery, wood floor, plywood ceiling.
- **Hall** (occupied concert hall) — walls 70 % plaster + 30 % wood panel,
  floor 70 % occupied seats + 30 % wood, plaster ceiling.
- **Church** (stone, congregation present) — walls 90 % rough stone (brick
  row) + 10 % glass, floor 40 % stone + 32 % wooden pews (plywood-panel row)
  + 28 % occupied seating, plaster ceiling. An empty dressed-stone church of
  this volume computes to 14–21 s, which is real (large cathedrals measure
  9–13 s) but useless behind a piano; the congregation and pews bring it into
  the 4–8 s band the profile targets.

## Computed Eyring RT60 per band (natural size)

`T60 = 0.161 V / (−S ln(1−ᾱ) + 4mV)`, air term included for every profile
(negligible below Hall size). These are the values the code reproduces at
`setSize(0.5)` and the values the engine-suite rows assert against (±25 % at
500 Hz).

| profile | 125 | 250 | 500 | 1k | 2k | 4k | mean ᾱ at 500 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Booth | 0.256 | 0.099 | **0.066** | 0.041 | 0.044 | 0.049 | 0.529 |
| Studio | 0.371 | 0.394 | **0.397** | 0.376 | 0.395 | 0.397 | 0.250 |
| Stage | 0.621 | 0.883 | **1.025** | 1.209 | 1.353 | 1.486 | 0.193 |
| Hall | 2.690 | 2.601 | **2.391** | 2.253 | 2.172 | 1.946 | 0.189 |
| Church | 6.750 | 7.066 | **6.506** | 5.609 | 4.770 | 3.316 | 0.081 |

Notes the numbers make on their own:

- The booth is boomy: drapery and carpet do almost nothing at 125 Hz
  (α 0.14/0.01), so LF rings 4× longer than mid. That is what small booths
  measure, and it is left in.
- The stage gets *brighter* with frequency (plywood panelling is a low
  frequency panel absorber), the hall and church get *darker* — material
  spectra plus the 4mV air term, which removes 0.45 s at 4 kHz for the hall
  and shortens the church top octave by ~1.5 s relative to no-air.
- Hall/Church HF < mid is engine-suite row (b).

## Size control

`setSize` scales the room's linear dimensions by `k = 2^(sizeNorm − 0.5)`
(0.71× … 1.41×, natural at 0.5). V scales k³, S scales k², so Eyring T60
scales ≈ k until the air term bites. Computed T60 at 500 Hz across the knob:

| profile | size 0.0 | size 0.5 (natural) | size 1.0 |
| --- | --- | --- | --- |
| Booth | 0.047 | 0.066 | 0.093 |
| Studio | 0.281 | 0.397 | 0.560 |
| Stage | 0.728 | 1.025 | 1.439 |
| Hall | 1.710 | 2.391 | 3.329 |
| Church | 4.746 | 6.506 | 8.817 |

Profile 0 (Current) keeps the shipped mapping `T60 = 0.6 + 3.4·s²`,
bit-exact.

## Early reflections (image sources, order ≤ 2)

24 images per profile: 6 first-order (one per wall) plus same-axis and
cross-axis second-order pairs. Per tap: delay `(r_i − r_d)/c` relative to the
direct sound, amplitude `(r_d/r_i)·Π√(1−α_wall)` per band (pressure
reflection factor per bounce), equal-power panned by lateral angle at the
listener. The band-dependent reflection loss is folded into one first-order
section per tap — a pole when the reflection darkens, a rising shelf when it
brightens (glass) — fitted at 125 Hz/4 kHz (tilt) and 500 Hz (exact).

First-arrival predictions (engine-suite row (c) asserts ±15 %):

| profile | first image | delay after direct | 500 Hz amplitude re direct |
| --- | --- | --- | --- |
| Booth | near side wall | **2.205 ms** | 0.375 |
| Studio | floor | **2.708 ms** | 0.695 |
| Stage | floor | **2.103 ms** | 0.806 |
| Hall | floor | 1.676 ms | 0.555 |
| Church | floor | 1.348 ms | 0.789 |

Latest kept image: booth 11.6 ms, studio 34.9 ms, stage 69.8 ms, hall
174.8 ms, church 233.0 ms (natural size; ×√2 at full size, which bounds the
tap buffer at 330 ms).

## Late field

The existing 8-line Householder FDN is retuned, not rebuilt. Two changes:

1. **Per-line decay filter.** The single lowpass + broadband gain cannot hold
   six band targets, so each line's `gain + one-pole` pair becomes one
   absorptive first-order section (Jot's design): the 125 Hz/4 kHz ratio of
   `g(f) = 10^(−3·L_i/(T60(f)·fs))` picks a pole when the tail darkens
   (booth, hall, church) or a zero-plus-pole rising shelf when it brightens
   (the stage's panel absorbers eat bass), and the overall gain lands `|H|`
   at 500 Hz on `g(500)` exactly. The shelf, not a bare zero: a bare zero
   anchored at 500 Hz keeps rising past 4 kHz, crosses unity loop gain
   before Nyquist, and the network self-oscillates (measured before the
   shelf landed: the stage tail grew instead of decaying). The shelf's pole
   flattens the response above 4 kHz so the section peaks at ~g(4k) < 1 and
   stays passive. The 500 Hz decay
   is then correct by construction (orthogonal feedback matrix ⇒ loop gain =
   per-line attenuation), band tilt approximates the other four points.
   Profile 0 keeps the original arithmetic untouched.
2. **Line lengths and pre-delay scale with the mean free path** 4V/S: booth
   3.6 ms, studio 8.3 ms, stage 16.2 ms, hall 37.7 ms, church 44.7 ms. Line
   lengths use the shipped prime set scaled by mfp relative to the set's
   51.0 ms mean; the late field enters after one mean free path, which is
   when a real room's reflections stop being countable.

**Early/late balance** follows the direct-to-reverberant relationship:
reverberant-to-direct pressure ratio `√(16π r_d²/R)` with room constant
`R = A/(1−ᾱ)` at 500 Hz (Kuttruff ch. 5): booth 1.11, studio 2.59, stage
2.57, hall 2.17, church 2.91. The FDN output is scaled by this ratio times a
single calibration constant (0.30, chosen once so the Studio profile's wet
level at equal mix matches the shipped Current room within ~2 dB; the
*relative* early/late and profile-to-profile levels are the computed physics).

## Switching behaviour (contract)

`setProfile(int)` with {0 Current, 1 Booth, 2 Studio, 3 Stage, 4 Hall,
5 Church}. Profile 0 is bit-exact with the shipped room (row (d)). Any
profile or size change on profiles 1–5 is click-safe: the wet path ramps to
silence over 15 ms, the new geometry is applied at the null (delay taps,
line lengths, filters — the things that cannot move continuously), and ramps
back over 15 ms. The tap input history buffer is kept across the swap, so
early reflections of notes already ringing are correct immediately after the
switch. Row (e) bounds the output second difference across a mid-ring
switch. Repeated calls mid-fade retarget the pending configuration —
the ramp itself is the rate limit.

## What is [T]

- Air attenuation m values (ISO 9613-1 derived; the fetched chart carries no
  air column).
- The "brick ≈ rough stone masonry" proxy: Everest lists coursed rubble
  stone near brick's values **[T]**; dressed marble is listed separately and
  is used for the church floor slab.
- Cathedral RT60 anecdotes (9–13 s empty) quoted for plausibility only,
  nothing in the code depends on them.
