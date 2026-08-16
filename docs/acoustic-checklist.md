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

Tracked here so they are not rediscovered as surprises:

- **Tine swing across the compass** spans about 4× in the model; a real one
  spans past 50× (tens of mm at the bottom, under 1 mm at the top). The hammer
  had to be graduated against the tine's effective mass to keep the treble
  collision stable, and that flattened the gradient.
- **Growl is not stronger in the bass than the middle**, for the same reason.
- **C2 is not met**: inharmonic content is still 18–35 dB up at 300 ms where it
  should be 78–92 down. It starts at the right level and does not die.

## Sources

- Real Rhodes Mark I samples: `github.com/sfzinstruments/jlearman.jRhodes3d` (CC BY-NC 4.0)
- Muenster & Pfeifle, ISMA 2014 — http://www.conforg.fr/isma2014/cdrom/data/articles/000062.pdf
- Pfeifle, DAFx-17 — https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf
- Shear & Wright, NIME 2011/2012 — tine dimensions, tip displacement, T60
- Gabrielli et al., JASA 148(5):3052 (2020) — **full text not obtainable**; only
  the companion audio at `github.com/LOGUNIVPM/rhodes-companion-files`
