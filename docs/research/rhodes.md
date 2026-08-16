# Fender Rhodes — reference

Working notes for the Epi model. The mechanical findings are cited in the code
comments where they are used; this file carries the electronics, which are not
modelled yet, and the corrections to claims that turned out not to survive
scrutiny.

## Corrections to widely repeated claims

Each of these was believed at the start of this work and is wrong:

1. **The tonebar is steel, not brass.** Repeated everywhere, including in
   Pfeifle & Muenster.
2. **The tine does produce its own overtones in the attack**, at −40 to −60 dB
   and stretched above the textbook cantilever ratios. The stronger claim of
   "no overtones at all" is not supportable. What survives, and what the model
   asserts, is that the tine's overtones are far below its fundamental while the
   pickup's are not.
3. **Pfeifle & Muenster contradict themselves** on whether the tonebar is
   audible in the sound.
4. **The pickup coil is not split into two counter-phase sections.** US patent
   4,040,321 says *"a large number of turns of fine wire around the bobbin 39 to
   thus form a coil 41"* — one coil — and the service manual draws two terminals
   per pickup. Hum cancellation happens at the harp-wiring level, by connecting
   half the pickups front-to-back. This is the second Pfeifle & Muenster claim
   to fall.
5. **The preamps are named after three designers, not two eras**: Jordan
   (pre-1969 Suitcase, mono tremolo, no panning), Peterson (80 W Suitcase),
   Haigler (100 W Suitcase *and* Janus I — one board, two names). "Janus II" as
   a preamp appears in no factory document.
6. **There is no Bass Boost on any Suitcase preamp.** That control is the
   Stage's.
7. **The Stage's "Bass Boost" is a bass cut that you back off.** It is flat at
   the boosted extreme.

## The pickup, electrically

| Parameter | Value | Source |
| --- | --- | --- |
| DC resistance, one pickup | ~180 ohm (170-190) | Vintage Vibe |
| Wire gauge | AWG 37 (0.0045 in, measured) | ep-forum |
| Magnet | **Alnico 5, 0.5 x 0.1875 in**, magnetised lengthwise | US 4,040,321 |
| Turns | **NOT FOUND** | |
| Inductance, self-capacitance, resonant peak | **NOT FOUND** | |

The missing L and C are a **confirmed gap, not a search failure** — a forum
thread exists asking for exactly them and nobody produced a number. Do not
invent them.

Harp wiring, 73-key: the early version is 12 series groups (one of 7 parallel,
eleven of 6); from the early 1970s to 1985 it is **24 series groups** (one of 4,
twenty-three of 3). **Total DCR ~1.4-1.6 kOhm**, calculated at 1425 and measured
in that range.

Two errors in one sentence of the service manual, worth knowing: it says the
rewiring *"quadrupled"* output — it **doubles** voltage, +6 dB; what quadruples
is source impedance. And its *"approximately 2500 ohms"* cannot be a DCR, since
that measures 1425.

## The preamps

**No published measurement of either preamp exists.** Everything below is a
numerical nodal solve of the factory schematic, done twice independently and
agreeing to about 1 dB.

### There is no inherent mid-scoop

At the centre detent the Haigler/Janus Baxandall stack is **flat to within
0.01 dB from 10 Hz to 20 kHz**. The famous "smile" is entirely a user setting:
both sliders at full boost gives a minimum of +1.86 dB at 359 Hz, which is
−16.4 dB against the 20 Hz shelf and −19.9 dB against the 10 kHz one.

Control authority is very broad and low-turnover: bass ±18.4 dB at 20 Hz but
only ±1 dB by 467 Hz; treble ±24.7 dB at 15 kHz but only ±3 dB at 557 Hz. That
is why Suitcase "treble" reads as bite rather than air.

### The voicing that matters most

Janus I, tone flat, relative to 1 kHz:

```
 20 Hz −16.0 │ 40 −10.3 │ 60 −7.2 │ 100 −4.0 │ 200 −1.3 │ 500 −0.1
  1k    0.0  │  2k −0.3 │ 5k −2.1 │  8k −4.2 │ 10k −5.6 │ 15k −8.4  dB
```

**The low E fundamental at 41.2 Hz is down about 12 dB before it reaches the
speaker, with the tone controls flat.** That is the quantitative form of "you
have to run a Suitcase with the bass up", and it is the single biggest thing to
get right in the output chain.

The input stage sets it: 0.1 uF into 10 kOhm gives a **159 Hz highpass** (127 Hz
with the harp as source), and 220 kOhm ‖ 120 pF gives a 6.03 kHz lowpass and a
gain of x22. Total preamp gain +33.7 dB.

The Mark II board (P/N 018019) uses the *same* tone stack at 1% values — within
0.2 dB everywhere — so **one tone-stack model covers the whole 5-pin family**.
Its real improvement is the input: 47.4 kOhm and a 33.6 Hz corner.

The volume pot is 5 kOhm audio loaded by a tone stack whose input impedance runs
**23.0 kOhm at 50 Hz down to 3.96 kOhm at 10 kHz**. A ~4 kOhm frequency-dependent
load on a 5 kOhm pot is **not a clean log divider**; do not model it as one.

### Peterson

Fully discrete, no op-amps — the drawing says *"ALL TRANSISTORS ARE 037119
(SELECTED 2N3392)"*, eight in the audio path. Passive tone stack, bass and treble
both 100 kOhm linear. Input highpass 26-32 Hz, so full bandwidth in the bass,
unlike the Janus. Solved: flat loss −6.1 dB, bass range 13.3 dB at 50 Hz, treble
6.8 dB at 10 kHz.

## The vibrato is panning, and its waveform is not a sine

Confirmed by topology: the mono signal splits into two identical inverting
stages, each fed through its own photoresistor, with the two LEDs driven from
the same LFO node through **oppositely poled diodes**. **No pitch-modulating
element exists anywhere in any version.**

**Sine is wrong, pure square is wrong, triangle is wrong.** The LFO is a square
wave in every stereo version; the shape comes from what follows it:

- **Peterson**: square into a 150 ohm / 50 uF RC, then a **#19 bulb filament's
  thermal inertia**, then an ORP61 photocell. That lag is the "cat's eye" shape.
- **Janus**: square into a shaper that clips it to a **hard trapezoid, flat-topped
  about 77% of the cycle**, then an LED network and a Vactrol with fast attack
  and slow decay.

Rate: Peterson 2.0-6.6 Hz calculated; Janus 0.30-30 Hz calculated, with the one
genuinely measured figure being *"at top speed, the frequency-meter shows 15 Hz"*
on a real unit. **Model the maximum at 15-30 Hz.** No measured minimum and no
oscilloscope capture of any Rhodes LFO exists.

### Depth is rate-dependent, and that is the important nonlinearity

Intensity is a bleed/overlap control, not a crossfade depth. At maximum it
reaches genuine silence on the off channel (−72 dB); at minimum, depth collapses
to 1.8 dB.

But the Vactrol is a **VTL5C1**: attack 2.5 ms, decay only guaranteed to
100 kOhm in 35 ms — and at 100 kOhm the off channel is only −20 dB down. So the
**effective depth is deep when slow and shallow when fast**. This is the most
important thing to model about the vibrato.

The pan law is **neither constant-power nor constant-amplitude**: the network
would be constant-amplitude only if `Ra·Rb = (9.09 kOhm)^2`, which real Vactrols
do not satisfy. The residual sum modulation is the well-known "vibrato thump",
and it scales with Intensity.

**Recommendation: an overlapping crossfade driven by asymmetric envelope
followers (2.5 ms attack, 35 ms decay) fed by a trapezoidal LFO, and let the
mono sum fall out. Do not impose a pan law.**

A factory revision the service manual omits: P/N 015243 adds 25 uF reservoir
caps across the LED drive nodes — an explicit slew limiter, about 25 ms on
release. This resolves an internal contradiction in the manual, whose parts list
and drawing are for different board revisions.

## The Stage output

Not in the service manual; documented by four independent reverse-engineerings
that agree, and confirmed verbatim by Rhodes Music Corp's own VP of Engineering.

```
harp hot --+--[ 50 kOhm REVERSE-AUDIO rheostat ]--+-- 10 kOhm audio volume --> tip
           +--[ 47 nF ]------------------------- -+
```

A high-frequency **shelf**, not a single pole:

| Bass Boost | zero | pole | bass vs treble |
| --- | --- | --- | --- |
| 0 ohm (full "boost") | — | — | **flat, 0 dB** |
| 10 kOhm | 339 Hz | 677 Hz | −6.0 dB |
| 50 kOhm (max) | 67.7 Hz | 406 Hz | **−15.6 dB** |

The reverse-log taper crowds nearly all the audible change into the first part
of the rotation, so a linear knob-to-ohms map will feel wrong.

Output impedance ~1.25 kOhm at volume max and boost 0, peaking at 2.86 kOhm
around 57% wiper, and ~8.4 kOhm with boost at maximum. Unchanged from late 1973
to 1985. The earlier circuit (~1970-73) used two 10 kOhm pots and a 1 uF cap;
**no schematic of it exists**.

## Three things worth building explicitly

1. The **159 Hz input highpass** on the Janus I — it is why the instrument needs
   its bass turned up.
2. The **rate-dependent vibrato depth** from the Vactrol's 35 ms decay.
3. The **reverse-log tapers** on Stage Bass Boost and both vibrato controls.

## Sources

- Muenster & Pfeifle, *Non-Linear Behaviour in Sound Production of the Rhodes Piano*, ISMA 2014 — http://www.conforg.fr/isma2014/cdrom/data/articles/000062.pdf
- Pfeifle, *Real-Time Physical Model of a Wurlitzer and Rhodes Electric Piano*, DAFx-17 — https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf
- Rhodes Service Manual — https://synthfool.com/docs/Other_Misc/Rhodes_Servicemanual.pdf
- US patent 4,040,321 (pickup construction)
- Factory drawings P/N 015243 and 018019, not in the service manual
- Falaize & Helie, JSV 390 (2017) — note their published eq. (22) is
  dimensionally wrong; use (25). Their pickup model is analytic, not fitted.
