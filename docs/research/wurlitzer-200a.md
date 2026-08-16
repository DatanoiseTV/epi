# Wurlitzer 200A — measured reference

Working notes for the Epi model. Everything here is either quoted from a
document, or measured from real audio and marked as such. Where a number could
not be found, it says so rather than guessing.

## The finding that decides the architecture

**The reed is one mode. The harmonics come from the pickup.**

Three independent lines agree:

1. Real 200A spectra are harmonic to within **±0.1 cent out to the 20th
   partial** (measured, single-note recordings with no loop points). A stiff
   cantilever's modes sit at 6.3×, 17.5×, 34.4× f0 and cannot do this.
2. No inharmonic peak above **−45 dB** anywhere between 3× and 25× f0 in the
   sustained portion of any note tested.
3. Pfeifle, DAFx-17 §5.5: *"the measurements show that the influence of higher
   modes are comparably small or non-existent even under extreme playing
   conditions, thus the primary mode of vibration can be approximated by the
   reeds first natural frequency"*.

So: one damped sinusoid per note, then a nonlinear pickup. **A modal bank at
harmonic ratios would double-count.** This is the same shape as the Rhodes,
where the tine is also a pure sine and the field manufactures everything.

## Decay and Q

Measured across nine notes, from unlooped single-note recordings:

| f0 (Hz) | dB/s | T60 | dB actually fitted |
| --- | --- | --- | --- |
| 55 | −1.11 | ~54 s | 1.4 (extrapolated, do not quote) |
| 124 | −3.40 | 17.6 s | 7.7 |
| 165 | −7.94 | 7.6 s | 9.1 |
| 277 | −7.39 | 8.1 s | 11.9 |
| 553 | −14.4 | 4.2 s | 22.5 |
| 787 | −25.4 | 2.4 s | 17.7 |
| 1664 | −21.3 | 2.8 s | 17.1 |

Bass loses ~1.2 dB/s, treble ~21–25 dB/s — a 20:1 ratio.

**Q ≈ 1000, flat across the keyboard** (median 970, mostly 860–1060), i.e. a
loss factor eta ≈ 1e-3. That is the signature of hysteretic material damping
with a near-constant loss factor. Cross-check: fitted only on 124 Hz and above,
it predicts 1.36–1.50 dB/s at 55 Hz; measurement gives 1.11–1.48. One parameter
covers the instrument.

Clamp loss dominates over material loss — repair practice confirms it (filing
the reed-bar knife edge changes sustain by seconds). **Expose clamp loss as the
sustain control; that is the physically correct place for it.**

**Per-partial decay is emergent, not a parameter.** If the harmonics come from a
memoryless nonlinearity on one decaying sinusoid, harmonic k must decay k×
faster in dB/s. Measured on the quietest note: ratios 1.58, 2.60, 3.03, 3.83,
4.10, 5.27, 6.28 for k = 2..8. Do not implement per-partial damping.

A **~2.4 Hz amplitude modulation** appears as symmetric sidebands around every
harmonic. That is the two-polarisation reed beat, not the 5.75 Hz tremolo.
It is much of why a static sine sounds dead.

## The pickup does not load the reed

Computed for a Db4 reed, 150 V bias, 0.5 mm gap, 1 MOhm load:

- electrostatic softening spring −0.048 N/m against a mechanical 539 N/m,
  i.e. **−0.077 cents**. Inaudible.
- power into the load 4.8e-9 W against 16.9 uJ stored: the pickup accounts for
  **~0.03% of the total loss**.

**Model it as a pure sensor: no back-action, no electrostatic detuning.**
(The softening goes as 1/d^3, so at a 0.2 mm gap it would reach ~1.2 cents.)

## Velocity and register

Energy in harmonics 2–12 relative to the fundamental (measured):

| note | pp | mp | f | ff |
| --- | --- | --- | --- | --- |
| A1 (55 Hz) | −4.9 | +8.4 | +15.6 | **+26.7 dB** |
| B2 (124 Hz) | | | +6.4 | |
| E3 (165 Hz) | | | +6.6 | +12.8 |
| Db4 (277 Hz) | −15.6 | | −3.6 | |
| Db5 (553 Hz) | | | −12.2 | −12.5 |

At ff the low A's fundamental sits ~18 dB *below* harmonics 2–7 — the note is
essentially all upper harmonics. Two laws: about **32 dB of harmonic growth
pp→ff** in the bass, and a monotonic fall with register until the top of the
instrument does not bark at all.

**Drive the nonlinearity with displacement relative to the gap and let the
register dependence emerge.** Do not hand-tune a per-key curve.

## Geometry

Reed length law, verified internally exact: reed #1 = 2 19/20 in, each 1/20 in
shorter to #20 = 2 in; #21 = 1 43/44 in, each 1/44 in shorter to #64 = 1 in.
Widths 0.151 in (bass) to 0.096 in (treble); tongue thickness 0.020 in early,
0.026 in on late 200A. Material: Swedish Sandvik steel.

Tip-mass ratio mu (computed from measured geometry): **~1.7 in the bass falling
to ~0.1 in the treble**. Order of magnitude with a reliable trend, not
precision. It must fall steeply, because from key 21 to 64 the pitch rises 12×
while the length only shortens 1.977×, which for f proportional to 1/L^2 buys
just 3.9×; the missing ~3× comes from unloading the tip.

Tip mass pushes the upper partials *further* from harmonic (f2/f1 rises from
6.27 toward 8–14). **No value of mu brings the reed's modes near a harmonic
series** — which is the point of the headline finding above.

## The 200 and the 200A are electrically different instruments

This trips up most secondary sources, which say "the Wurlitzer preamp" without
saying which. Read directly from the Series 200 service manual (p.4-5):

| | Series 200 | 200A |
| --- | --- | --- |
| Reed-bar polarizing rail | **160-170 V DC** | **+150 V** |
| Reed-bar preamp | three transistors | two |
| Tremolo modulator | a **diode** shorting a feedback path | **LED/LDR optocoupler** |
| Oscillator | **phase-shift**, Darlington | **twin-T** |
| Rate | **5.75 Hz, preset** | ~5.6 Hz |
| Output stage | complementary | quasi-complementary |
| Low-voltage rail | 42 V raw to 15.5 V +/-20% | +15 V |

So the often-quoted **5.75 Hz belongs to the Series 200's phase-shift
oscillator, not the 200A's twin-T. Do not cross-apply it.**

On the bias voltage specifically, the Series 200 manual p.4 says verbatim: *"The
reed pickups, mounted on the reed bar are 160 to 170 volt DC above ground"*. So
Pfeifle's *"according to the manual, 170 V"* is **correct and correctly sourced**
— he was reading the right manual for the instrument he measured. The 200A is a
different rail: its assembly drawing labels the harness *"BLACK +150V TO PREAM &
REED BAR"*. Real instruments sag below nominal; Pfeifle measured 130 V.

**For a 200A use 150 V nominal.** The Series 200 ran 10-20% hotter, which by
itself changes pickup sensitivity between the two models.

## The feed resistor, and a 23% error worth avoiding

The Series 200 manual states plainly that **R-56 = 560 kOhm**. `openwurli`
assigns it 1 MOhm. If that value carries into the 200A:

```
openwurli:   1M   || 402k = 287 kOhm -> tau 68.9 us -> corner 2312 Hz
manual R-56: 560k || 402k = 234 kOhm -> tau 56.2 us -> corner 2834 Hz
```

A **23% shift in the pickup high-pass corner**, which lands directly on the
bass-vs-treble balance since this filter is the instrument's main register
balancing mechanism. Worth verifying against schematic 203720-S-3 before
committing, because the 200A supply was redesigned and the feed resistor may
have changed with it.

## Action geometry — the velocity mapping

All verbatim from the service manual, pp. 16-19. Blow distance minus let-off
gives the free-flight segment directly, which is what converts key velocity into
hammer-at-reed velocity.

| Parameter | Imperial | Metric |
| --- | --- | --- |
| Hammer blow distance (tip to underside of reed) | 1 5/64 in | **30.95 mm** |
| Let-off | 1/8 in | **3.18 mm** |
| Key dip (front of naturals) | ~3/8 in | 9.53 mm |
| After-touch | ~1/32 in | 0.79 mm |
| Damper rest clearance | ~1/32 in | 0.79 mm |
| Reed-bar shim for duds/short ringers | .010-.025 in | 0.25-0.64 mm |

## Voicing, and what it implies about the plate geometry

Manual pp. 43-44, verbatim: *"Check to see if the reed involved is slightly off
center in the pickup or electrode... If the reed is centered and still too loud,
bend the ends of the pickup up slightly (1/32" to 1/16")."*

"Off center **in** the pickup", plus the fault-chart entry *"Reed shorted against
pickup"*, is language for a reed sitting *inside* an opening. That favours
Pfeifle's flat slotted-comb geometry over a U-channel reading. Not conclusive,
but it is the first primary-source evidence either way.

**No absolute vertical gap dimension appears anywhere in the manual** — the gap
really is undocumented, confirmed against the primary source rather than assumed.

## Other verified specs

Range **64 notes, A1-C7 (MIDI 33-96)**. Damper gap 0.035 in. Hammers 3-ply
maple with mothproofed felt. Model 200 speakers: two 4x8 in oval. Power
consumption 40 W (this is consumption, not the 20 W amplifier rating).

## Caution on openwurli

`hal0zer0/openwurli` implements the same tip-mass characteristic equation, and
**its beta2 column is wrong** — at mu = 0.5 it gives f2/f1 = 9.905 against a
correct 8.382, an 18% error in the second partial. Its solver does not converge
to the clamped-pinned limit as mu goes to infinity. Do not inherit that table.

Its *pickup* model is well posed and worth reading: C(y) = C0/(1−y), bilinear
time-varying RC, 287 kOhm × 240 pF giving a 2312 Hz corner.

## Sources

- Pfeifle, *Real-Time Physical Model of a Wurlitzer and Rhodes Electric Piano*, DAFx-17 — https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf
- *Electronic Pianos Series 200 and 200A Service Manual*, The Wurlitzer Company — https://manuals.fdiskc.com/flat/Wurlitzer%20Series%20200%20Service%20Manual.pdf
- Reed dimensions — https://docwurly.com/wurlitzer-ep-history/wurlitzer-ep-reed-compatibility-history/
- Gabrielli et al., EURASIP JASP 2013:103 (Clavinet, methodologically the closest template) — https://asp-eurasipjournals.springeropen.com/articles/10.1186/1687-6180-2013-103

**Still missing:** Pfeifle & Muenster, *Tone Production of the Wurlitzer and
Rhodes E-Pianos*, Springer 2017, DOI 10.1007/978-3-319-47292-8_3 — closed
access, and almost certainly holds the reed dimensions, pickup gap and FEM mode
shapes.
