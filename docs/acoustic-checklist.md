# Acoustic accuracy checklist — Rhodes

Every row is a number measured on a real instrument, with the source. The
suite in `tests/test_epi_reference.cpp` checks the model against them and
prints the measured value beside the target, so "it sounds wrong" becomes
"rows 3, 6 and 9 are out".

**Reference instrument**: 1977 Fender Rhodes Mark I Stage 73, sampled unlooped
by its owner, 15 roots F1–C7 across 5 velocity layers. Cross-checked against a
second, unattributed Rhodes set and against the published literature. Direct
from the harp, so absolute harmonic balance carries a fixed recording EQ;
decay, pitch, inharmonicity and *relative* behaviour are unaffected.

---

## A. What the harmonics do

| # | Property | Target | Source |
| --- | --- | --- | --- |
| A1 | Partials are exact integer multiples | within **1 cent** out to H10 | measured, both sets |
| A2 | H2 − H1, low/mid register, hard velocity | **+6 to +24 dB** (H2 dominant) | measured, Table 1/2 |
| A3 | H2 − H1, same notes, soft velocity | **−10 to −40 dB** (H1 dominant) | measured |
| A4 | Swing in H2−H1 from softest to hardest, same note | **16 to 37 dB** | measured |
| A5 | H2−H1 crossover with register | H1 takes over above ~200–500 Hz | measured; voicing-dependent |

A1 is the strongest single constraint: **there is no string-like stretching**.
Anything that puts the harmonics off an exact integer series is wrong.

## B. How they decay

| # | Property | Target | Source |
| --- | --- | --- | --- |
| B1 | Decay rate ratio H2/H1, soft velocity | **2.06 – 2.21** | measured, Table 4 |
| B2 | Decay rate ratio H3/H1, soft velocity | **3.10 – 3.42** | measured |
| B3 | Same ratios at hard velocity | compress and scatter (1.0–4.2) | measured |
| B4 | H1 T60, A2 / D4 / E5 / C7 | **28 / 19 / 5.7 / 1.7 s** | measured, Table 3 |
| B5 | H1 envelope is not a single exponential | 1-exp residual 1.4–5.1 dB RMS; 2-slope 0.2–1.9 | measured, Table 5 |
| B6 | Hard bass strike: H1 *rises* before decaying | **+2.2 to +3.9 dB/s for 4–6 s** | measured |

B1 and B2 are the load-bearing ones. Harmonics decaying at *exactly* k times
the fundamental's rate is the fingerprint of a **static nonlinearity acting on
one sinusoid** — it is what proves the harmonics are made by the pickup rather
than being independent modes. A modal bank with per-partial damping cannot
produce it.

## C. The attack

| # | Property | Target | Source |
| --- | --- | --- | --- |
| C1 | Inharmonic content at 10 ms, rel. loudest harmonic | **−12 to −20 dB** (range −8 to −42) | measured, Table 6 |
| C2 | Same content by 300 ms | **−78 to −92 dB** — genuinely gone | measured |
| C3 | Its decay rate | **−27 to −680 dB/s** (T60 0.05–1.7 s) | measured |
| C4 | Ratio of inharmonic to fundamental decay rate | **2.6× (bass) to 79× (treble)** | measured |
| C5 | Attack time, 10→90%, bottom octave → top | **14–21 ms → 0.6–1 ms** | measured, Table 9 |
| C6 | Hammer contact duration | **6.42 ms** average | ISMA 2014 |

## D. The tonebar mode

| # | Property | Target | Source |
| --- | --- | --- | --- |
| D1 | It sits **below** the fundamental | 0.65×f0 at 79 Hz → 0.11×f0 at 1969 Hz | ISMA 2014 Table 1 |
| D2 | It is a roughly fixed absolute band | **51–222 Hz** across the whole compass | derived from D1 |
| D3 | The pickup mixes it with f0 | sidebands at \|mode ± n·f0\| | measured, and the JASA companion audio |
| D4 | It is one mode, not a partial series | — | do not hand-place a half-integer series |

## E. Steadiness

| # | Property | Target | Source |
| --- | --- | --- | --- |
| E1 | Slow AM on H1, peak-to-peak | **0.02 – 0.6 dB** | measured, §7 |
| E2 | Implied bound on any second detuned component | **≥29 dB down**, typically −37 to −58 | derived from E1 |

E1 is why the tine is modelled in one plane. Two comparable components a few
cents apart produce many dB of swing and read immediately as chorus.

## F. Pitch

| # | Property | Target | Source |
| --- | --- | --- | --- |
| F1 | Initial sharpness, bass, hard velocity | **+9 to +29 cents**, settling in ~200 ms | measured, §8 |
| F2 | Same above D3, or at soft velocity | under 2 cents | measured |
| F3 | Per-note tuning scatter on a real instrument | −16 to +11 cents, idiosyncratic | measured |

F1 is a large-amplitude stiffness effect and it is real. The failure mode to
avoid is not its size but its duration: it must settle in a fifth of a second,
not over seconds.

## G. Brightness

| # | Property | Target | Source |
| --- | --- | --- | --- |
| G1 | Steady centroid, softest → hardest, same note | rises **2.5 – 6×** | measured, Table 8 |
| G2 | Steady centroid, F1 → E5 at hard velocity | rises only ~1.5× (nearly flat in Hz) | measured |
| G3 | Attack brighter than sustain | only at **high** velocity (1.10–1.84×) | measured |
| G4 | At soft velocity the attack is *duller* than the sustain | 0.72–0.94× | measured |

---

## Known gaps

- **The striking line now follows the service manual, and the collision was
  recalibrated around it.** The manual gives the hammer's contact point as
  57.15 mm from the tone generator at the extreme bass and 3.175 mm at the
  extreme treble — about a third of the free length falling to about an
  eighth — and the model now interpolates that distance in log over the FULL
  A0–C8 compass (anchoring it on the voicing register instead parked the
  extreme-treble eighth-inch on everything above E6 and collapsed the top
  octave's attack, and moved D4's strike toward the clamp far enough to fail
  its decay-ratio and brightness rows). The pieces that had been calibrated
  against the old geometry were re-derived with the whole suite as judge:
  the hammer-mass graduation now RISES out of the bass (2.2 g at A0 to
  6.6 g at the upper-mid knee, then tracking 0.30x the strike-point
  effective mass down through the treble) because the manual's line meets
  the bass tine at a twentieth of the old effective mass; the velocity
  floor moved 0.18 to 0.195 m/s, and 0.183 when the let-off was later put
  on the manual's graduation; and the residual axial restraint behind
  the large-amplitude sharpness recalibrated from 5% to 3.5% for the same
  F1 glide at the corrected amplitudes. Net effect on the tracked rows:
  C2 and F2 closed, C1's bass rows moved from the band edge to mid-band,
  B6 tripled its rise, and the hard-velocity tip swing now runs 3.9 mm at
  the bottom, 5.5 mm at its mid peak, 0.41 mm at C7 — against 3.1 / 3.9 /
  0.29 measured.


- **96 kHz does not fit.** Measured under the adversarial stress (every
  control sweeping, twenty notes a second, pedal cycling), 57% of blocks miss
  the deadline at 96 kHz where 48 kHz misses 0.1%. The honest cause is that the
  4x pickup oversampling is pure waste at 96 kHz — the flux content above the
  fold is already down 60–100 dB at 48 kHz internal rates — but the
  oversampling factor is compile-time and unwinding it is a real refactor, not
  a constant change. Until then: run the plugin at 44.1/48 kHz.


Tracked here so they are not rediscovered as surprises. Run
`ctest -R epi_reference` for the current numbers; this is the standing list of
what is understood and unfixed.

- **B5, the envelope is a single exponential** (0.53 dB residual against a
  measured 1.4–5.1). Two mechanisms were built and measured before this was
  accepted as a gap; both are recorded under "Ruled out" below. The
  conclusion they force: the real instrument's second slope, coexisting as
  it does with E1's tiny AM bound, cannot be a comparable second oscillator
  at the pickup — it is the transduction gain moving with amplitude, the
  same field hand-back that B6 measures, and it is short for the same
  reason (the field map's far tail, see G2).
- **C5, the bass attack is fast**: 6.3 ms against a measured 14–21, and it
  is NOT contact-limited: softening the bass hammer tip until the contact
  stretched from 3.3 to 12 ms moved this number by nothing. What the row
  measures is the note's own quarter-period rise to the first flux peak.
  The real 14–21 ms implies the real strike keeps building the swing across
  two periods — a heavier hammer riding longer — and the swing rows (A2,
  F1, F2) pin the hammer mass well below that. Reconciling both needs a
  tine-side effective mass the current beam-plus-patch collision does not
  produce, not a constant.
- **B6, a hard bass strike's fundamental rises at a third of the measured
  rate**: +1.28 dB/s against +2.2 to +3.9, from +0.58 before the striking
  line moved. The fence is now measured precisely: B6 and A2 read the same
  render, A2's H2 dominance sits within 2 dB of its measured +24 dB
  ceiling, and with this field map more swing moves both together. The
  real pole buys its extra rise by pushing energy into H6–H15 instead —
  the same missing far structure as G2.
- **G2, the bass is not rich enough relative to its own pitch**: the steady
  centroid rises 6.1× from A1 to E5 where the reference rises about 1.5 while
  the fundamental rises twelvefold. A hard bass note's spectrum has died by its
  eighth harmonic; the real one carries energy past its thirtieth. The fix is
  a field map with honest far structure — the flat's edge and the wedge that
  the current map smooths over off-axis — and it is the shared root cause
  behind B5 and B6 above. Note the one caveat on this row: the reference
  recordings carry a fixed recording EQ, and a treble-tilted one inflates the
  bass centroid more than the treble's, so the target may be somewhat high.
  The direction is not in doubt.
- **C1 at E5 is 2 dB too clean** (−43.9 against a band floor of −42): the
  wrong kind of perfect — the strike transient the patch-and-dwell weighting
  leaves at E5 is slightly quieter than the reference's quietest sample.
  Tracked with a bound at −52 so it cannot silently get cleaner still.

### Ruled out, with the measurement

- **B5 via a second polarisation at the pickup.** Letting the horizontal set
  modulate the gap — at any coupling, any amplitude, any decay split — moved
  the B5 residual from 0.53 to at most 0.54 dB while B1 went from 2.22 to
  2.75: a parallel channel at the pickup bends the fitted H1 slope out of
  B1's band long before it registers as envelope residual.
- **B5 via mechanical exchange between the polarisations.** A passive
  quadratised spring coupling the two fundamentals (the Weinreich two-slope
  mechanism: degenerate frequencies, damping-split eigenmodes) preserves B1
  exactly as the theory says it must — 2.21 across the whole sweep, because
  every harmonic squares the same composite envelope — but the overdamped
  regime needs the polarisations degenerate to under a tenth of a cent.
  At the realistic detuning the exchange turns oscillatory: E1's AM rose
  from 1.34 to 1.73 dB pk-pk (bound 1.5) with B5 still only at 0.75, and
  B4 at D4 sagged to the bottom of its band. Both experiments reverted.

- **The stiffness cap on the hammer contact is not binding anywhere.** The code
  notes that the principled fix is to carry the contact's elastic part through
  the quadratised path, which would remove the explicit time-step limit
  entirely. Measured across the compass, the cap never engages, so that work
  would change nothing. Worth revisiting only if the hammer is re-sized.
- **The growl is not sitting behind the pickup gap.** The argument for closing
  it is good -- the field is smooth far from the pole and nearly a cusp close
  in, and at 0.6 mm the hard bass fundamental rises at 2.8 dB/s, which is
  exactly what B6 asks for. It breaks nine other rows doing it: the tine is
  swinging two pole-widths, so it leaves the field entirely twice a cycle, and
  that spike puts the inharmonic floor up 45 dB, starts the fundamental beating
  against itself, and collapses the middle-register attack to half a
  millisecond. Swept over both voicing controls against the whole suite, the
  best setting is within a hair of the shipped one.

### Retired gaps

Kept briefly because each was believed to be a defect in the model and was not:

- "Inharmonic content sits at −18 to −35 dB at 300 ms" — a measurement error.
  The residual was computed against only sixteen harmonics, so 30 dB of
  ordinary harmonic energy above H16 was being counted as noise. Measured
  properly it was already −40 to −72, and after the staircase fix below,
  −57 to −73.
- "Harmonics decay at the same rate as the fundamental" — also measurement.
  The comb was cut to the analysis frequency rather than to the fundamental,
  so measuring H2 mostly measured H1 leaking through at −11.8 dB.
- "Two controls do nothing" — `strikeNoise` and `spaceMix` were declared,
  shown, saved in presets and read into the engine while the DSP never touched
  either. Both are implemented now, and row S4 catches any third one.
- "The partials are 10 cents off an exact series" and "the attack is three
  times brighter than the sustain when played softly" — both real, both one
  bug: the quiet-tine fast path held one value across all four oversampled
  subsamples instead of interpolating the tip's path, and the resulting
  staircase put a flat comb of odd harmonics into every quiet note.

## Sources

- Real Rhodes Mark I samples: `github.com/sfzinstruments/jlearman.jRhodes3d` (CC BY-NC 4.0)
- Muenster & Pfeifle, ISMA 2014 — http://www.conforg.fr/isma2014/cdrom/data/articles/000062.pdf
- Pfeifle, DAFx-17 — https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf
- Shear & Wright, NIME 2011/2012 — tine dimensions, tip displacement, T60
- Gabrielli et al., JASA 148(5):3052 (2020) — **full text not obtainable**; only
  the companion audio at `github.com/LOGUNIVPM/rhodes-companion-files`

## The pickup field map: what the rebuild fixed, and what it priced

The three remaining reference gaps (B5, B6, G2) were traced to one root
cause: the field map's missing far structure. The map was rebuilt from
the real pole geometry and the result splits cleanly in two.

**Adopted -- the two-sheet field.** Horton & Moore's geometry, which they
validated against Hall-probe measurements to within plot resolution: a
magnetised slug is two sheets of magnetic charge, +sigma on the ground
face and -sigma on the flat end one magnet length behind it (Alnico 5,
0.5 x 0.1875 in, US 4,040,321). The map integrated only the near sheet,
which is a monopole with no far structure. The pair collapses far more
steeply where a hard bass tine actually swings and reverses sign at
about 8 mm, where the flux is on its way back around the magnet. The
footprint is now the slug's disc (a chord taper) rather than an infinite
strip, and the third dimension is integrated in closed form, so it costs
nothing. Suite: `fail=0`, gaps unchanged at 5 -- every calibrated row
holds, and the far field is honest now whether or not it moves a row.

**Measured and NOT adopted -- the quadratic transduction.** Reciprocity
says the flux is a product of two fields, not one: the tine is steel the
magnet magnetises (m ~ chi_eff B, with the demagnetising factor pinning
chi_eff regardless of the steel's permeability), and what that moment
puts through the winding is m . B_coil / I. Treating the transduction as
linear in B, as the model does, silently sets B_coil constant.

| law | B6 (band +2.2..+3.9) | A2 E2 (ceiling +24) | A4 E3 (ceiling 37) | fails |
| --- | --- | --- | --- | --- |
| linear in B (shipped) | +1.31 | +22.0 | 36.7 | 0 |
| B x B_coil, coil r = 2.6 mm | +2.23 | +24.2 | 39.2 | 3 |
| B x B_coil, coil r = 3.2 mm | +1.86 | +23.6 | 38.4 | 1 |
| B x B_coil, coil r = 4.4 mm | +1.49 | +22.5 | 37.3 | 1 |
| B squared (B_coil = B) | +2.78 | +24.8 | 40.9 | 4 |

B6 -- the gap this was aimed at -- closes as soon as the transduction is
a product, and the coil's radius interpolates smoothly between the two
limits (a wide coil weights every position alike, a coil as narrow as
the pole makes the weighting the field itself). But A4, the velocity
SWING, runs past its measured ceiling across the whole physical range of
the bobbin (it cannot be narrower than the 2.38 mm slug it wraps), and
A4 is not independent: it is A2 minus A3, so the model sits at a corner
where both endpoints are legal and their spread is not. The tine's own
diameter (1.905 mm, wider than the pole's effective half-width) was
added as a cross-section average of the flux, which is physically
required and softens the aperture, and it does not resolve the corner.

So the product law is right and the chain around it is not ready for it:
adopting it means re-deriving the velocity mapping that A2/A3/A4 fence
together. That re-derivation was then done, and it is written up below.

## Re-deriving the velocity chain: one fix, and four measured dead ends

The chain is MIDI velocity to launch speed, through the hammer's mass and
tip and the let-off, into the contact, out as tine swing. A2 and A3 fence
its two ends and A4 is their difference, so the whole thing is pinned by
three rows that are really two. Each link was taken in turn.

**Adopted -- the let-off is now the manual's graduation.** Figure 4-2 of
the service manual gives the escapement as a BAND at three points on the
rail: 6.35-9.53 mm at the extreme bass, 1.59-3.18 mm at tone bar 41,
0.79-2.38 mm at the extreme treble. The model carried a straight ramp
instead, and it was wrong in three ways at once: it sat about 12% BELOW
the band at both ends of the compass and above it in the middle, and it
was linear in register where the figure is convex -- a straight line
between the documented ends would put bar 41 at 3.6 mm against the 1.6
that is printed there. It is now interpolated in log between the three
measured points, one segment either side of bar 41, the way the striking
line and the tine lengths themselves progress; and the control spans the
documented tolerance band and nothing outside it, so the shipped default
is a regulated instrument rather than one at the edge. The treble let-off
more than doubles (0.70 to 1.55 mm at E6), the low mid loses about half a
millimetre. Because the free flight is what taxes a soft blow, the launch
floor was re-derived at the same arrival speed on the reference bass note:
sqrt(0.195^2 - 2 g 0.42 (4.65 - 4.08) mm) = 0.183 m/s. Suite: `fail=0`,
gaps unchanged at 5, and both fenced margins slightly better than before
(A3 at E2 -10.6 to -10.9, A4 at E3 36.7 to 36.6).

**Measured -- the launch floor is pinned to three percent.** Swept with
everything else held, A3 at E2 crosses its -10 dB ceiling at a floor of
0.203 m/s and A4 at E3 crosses its 37 dB ceiling at 0.192: floor 0.15
gives -14.5 dB and 41.0 dB, 0.17 gives -12.4 and 38.8, 0.195 gives -10.6
and 36.7, 0.21 gives -9.8 and 35.7. The pianissimo end of the chain has a
window of a few parts in a hundred and no headroom to hand anything.

**Measured and NOT adopted -- the hammer as one moulding.** The manual has
one plastic hammer body across the compass and graduates the TIPS, and
`rhodes-mechanics-open-questions.md` computes the effective tine mass at
the manual's striking line as nearly constant (11 g falling to 5.5 g), so
one hammer should work everywhere. Replacing the rising ceiling (2.24 g at
A0 to 6.6 g at the upper-mid knee) with a constant one was measured from
3.0 to 11 g. It does exactly what that research predicts: at 3.7 g the ff
tip swing becomes monotone with pitch, which closes BOTH known gaps in the
DSP suite -- the swing span across the keyboard and the growl gradient --
and it takes G2 from 6.12 to 4.61 and A4 at E3 from 36.7 to 33.6. It also
breaks four measured rows, from both sides at once. Below 3.8 g the mid
register goes flat: G1 at D4 falls to 2.53 against a 2.5 floor, G4 rises
to 0.98 against a 0.94 ceiling (a soft attack that is no longer duller
than its own sustain), and C2 at E3 over-cleans to -94.4 dB against a -92
floor. Above 3.8 g the bass goes hot: A3 at E2 reaches -9.8 and A2 at E2
+25.4. The window is 0.3 g wide and nothing fits in it.

The reason it cannot be one moulding *here* is worth recording, because it
names the next piece of work rather than this one. The model's effective
tine mass at the strike point runs 114 g at A0 down to 7.5 g at C8, a
fifteenfold spread, where the manual's rail gives twofold. Its strike
fraction sits near 0.25 across the whole compass instead of falling from
about 0.36 to 0.17, because the beam solve makes the bass tines some 40%
longer than Shear measured them -- 220.9 mm at A0 against 157, 180.4 mm at
E1 against 128-135 -- and a real tine is shorter than the bare cantilever
because its tuning spring loads the tip. The hammer graduation is
compensating for that. One moulding needs the tine geometry first.

**Measured and NOT adopted -- the manual's tip-hardness graduation.** The
tip table (Shore A 30 for hammers 1-30, 50, 70, 90, then wrapped) converts
through Gent's relation to a modulus span of about 1.1 to 50 MPa, and the
research file argues correctly that this, not mass, is where the real
graduation lives. Applied to the contact stiffness it collapses the treble
contact to 1.0 ms and throws the top two octaves out to 2.5 mm of swing
where they should move a fraction of a millimetre -- the exact failure the
code comment beside the stiffness already predicted. It needs the same
tine-geometry work first.

**Measured and still NOT adopted -- the product transduction, re-run on
the corrected chain.** With the let-off fixed and the floor re-derived,
and with the tine's own 1.905 mm cross-section averaged into the flux as
the physics requires:

| coil mean radius (mm) | B6 | A2 at E2 | A3 at E2 | A4 at E3 | verdict |
| --- | --- | --- | --- | --- | --- |
| linear in B (shipped) | +1.31 | +22.0 | -10.9 | 36.6 | ships |
| 2.6 | +1.92 | +23.6 | -10.7 | 38.1 | A4 fails |
| 3.2 | +1.61 | +23.0 | -10.6 | 37.4 | A4 fails |
| 3.6 | +1.45 | +22.7 | -10.6 | 37.0 | A4 at the ceiling |
| 4.4 | +1.25 | +22.1 | -10.6 | 36.4 | passes, B6 below the shipped law |
| 5.5 | +1.16 | +21.8 | -10.6 | 36.1 | passes, B6 below the shipped law |

The law's whole effect is a function of how tightly the winding hugs the
slug, and the two ends of that range are useless in opposite directions:
inside 3.6 mm it buys real rise and fails A4, and by the radius where A4
fits it has stopped doing anything -- at 4.4 mm and beyond the fundamental
rises MORE SLOWLY than under the plain linear law. The documented winding
puts the mean turn radius in the upper half of that range: 180 ohms of
fine wire on a bobbin over the 2.38 mm slug is a build of one to three
millimetres, so the honest radius is around 3.2 to 4 mm, which is either
just failing or already inert.

So the conclusion after re-deriving the chain is the opposite of a green
light. The product law is still the right physics and it is still not
adoptable, and the reason has moved: it is no longer that the velocity
mapping has not been checked, it is that the mapping is pinned to a few
percent by A3 and A4 together, the hammer graduation is pinned by four
more rows, and the transduction's own free parameter has no setting that
is both physical and useful. What separates B6 from A2 is not in the
chain and not in the transduction law -- it is the far structure of the
field itself, which is what G2 has been saying all along.

One inconsistency surfaced by this work and deliberately left open,
because it decides how the tine geometry above should be judged and
cannot be settled from the files on disk. The known-gap entry at the top
of this document records the model's hard-velocity tip swing as 3.9 mm at
the bottom against "3.1 measured"; `rhodes-mechanics-open-questions.md`
records the same quantity as 25-50 mm at the longest tine, attributed to
Shear section 3.1, and derives the strike-point swing independently from
the manual's escapement (6.35-9.5 mm in the bass, which the tine must not
exceed or it strikes the resting hammer on the way back down). Those are
an order of magnitude apart. The model currently sits at the small
figure, and its strike-point swing at the bottom is 0.9 mm against the
manual's 6-plus. Whichever is right, the hammer graduation and the tine
lengths are being judged against it.

Until then B5, B6 and G2 stay gaps with these tables as their price, and
the shipped map keeps the honest far field without the transduction
change.
