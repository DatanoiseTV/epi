# Yamaha CP-70 / CP-80 — reference

Working notes for the Epi model, from the service manual, parts list, operating
manuals and Yamaha's own factory alignment procedure. Numbers here come from
documents that were downloaded and read, not from search snippets. Where
something could not be found it says so.

## Corrections to assumptions this project started with

Four of the premises in the original plan are wrong, and they matter:

1. **Compass.** The CP-70 is **E1-E7, 73 keys (MIDI 28-100)**, not A0-A6.
   Confirmed three ways: the CP-70B spec sheet (*"73 keys from No. 8E to No.
   80E"*), the parts-list key assembly, and Yamaha's own MIDI implementation
   chart for the CP-70M. The CP-80 is 88 keys, A0-C8 (MIDI 21-108).
2. **Hammers are not felt.** Service manual: *"Hammers: Rubber (urethane) +
   artificial leather"*. The CP-70B spec calls them *"synthetic 'buckskin'
   hammers"*. Harder and less lossy at high frequency than voiced wool, so a
   shorter contact and a brighter excitation spectrum.
3. **Never three strings per note.** One string for keys 1-22, two thereafter,
   on both models. Independently confirmed by Modartt, who physically modelled
   a CP-80 and report single strings up to MIDI 42 — key 22 exactly.
4. **Dampers stop at A6** on both models, as on an acoustic grand. Notes above
   are undamped.

## The scale is the story

The longest bass string is **26 3/4 in (679 mm)** against roughly 2130 mm on a
concert grand — about **3.1x shorter**.

The consequence is visible in the stringing. The copper-wound section runs up to
**D#4 on the CP-80 and F4 on the CP-70** — roughly half the compass, well past
middle C, where a normal grand's wound section ends down in the bass. Winding
exists to add mass without adding bending stiffness, so having to wind that far
up is a direct measure of how far the scale was compressed. Cross-checked
against Yamaha's own prose: the CP-70B spec says *"First 59 strings made with
highest grade solid copper wrapping"*, and counting the parts list gives 15
single-strung plus 22 double-strung wound notes = exactly 59.

Inharmonicity follows `B = pi^3 E d^4 / (64 T L^2)`, or eliminating tension,
`B = pi^2 E d^2 / (64 rho f0^2 L^4)`. **B scales as d^2/L^4**, and the CP is
short *and* thick-strung for its pitch. Both push it up hard; the L^-4 term
dominates.

**No measured B for a CP exists in any source reached — NOT FOUND.** What is
documented is Yamaha's own consequence of it. From the service manual, p.14:

> *"Because of the nature of the harmonics of a vibrating string, it is
> generally possible to have a string correctly tuned and still hear a 'beat'
> when the string is sounded simultaneously with another tone one octave higher.
> This phenomenon may be more pronounced in the CP-80 due to its basic design
> concept aiming at the maximum portability."*

Yamaha's factory stretch-tuning table, cents from equal temperament:

| key | 1 | 10 | 20 | 30 | 44 | 49 | 60 | 70 | 80 | 88 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cents | −23 | −11 | −5 | −3 | 0 | 0 | +4 | +8 | +20 | **+35** |

Note the steep rise above key ~75: +11 to +35 in thirteen semitones.

String gauges are given exactly in the parts list, by key number, as Yamaha
music-wire sizes. The two instruments use **identical wire for identical notes
from C5 to C7** — the same treble scale design. The CP-80's top C8 is #13.5
(0.81 mm) against a normal piano's #13 (0.79 mm), so the top is only marginally
thicker; the compression is all in the bass and middle.

## There is no bridge and no soundboard

Each note's **piezo element is its own bridge**. The service manual's wiring
fold-out shows a small block clamped to the frame by a screw and compression
spring, with the note's string or strings bearing across it. One unit per note,
part **BD500020**; both strings of a bichord bear on the same unit. So the
termination is rigid metal, per-note and discrete, not a continuous wooden
bridge on a board.

The frame is a one-piece casting with integral struts and no wooden bridge at
all. **Its material — cast iron versus alloy — is NOT FOUND in any source; do
not assume.**

What the piezo senses is the **transverse force the string exerts on its
termination**, not radiated pressure. There is no soundboard transfer function
between string and signal, so none of an acoustic piano's radiation shaping,
board resonances or directivity is present. A bridge piezo also integrates all
partials at the termination, where a magnetic pickup samples the string at one
point and nulls any partial with a node there — Yamaha's stated design intent.

## The preamp is a large part of the timbre

This is the most under-reported technical fact about the instrument, and it is
measurable from the actual component values rather than inferred.

Input stage: the pickup bus feeds a 100 nF blocking capacitor into a **2SK30A
JFET gate loaded by 470 kOhm**. There is **no op-amp anywhere in the audio
path** — nine discrete JFET stages, and the only IC is the tremolo LFO.

The tone control is a **passive, cut-only shunt network**:

| Branch | Series element | Variable | Corner |
| --- | --- | --- | --- |
| Bass | 0.47 uF tantalum | A50 kOhm | 6.8 Hz |
| Middle | 0.47 uF tantalum | A1 kOhm ‖ 39 kOhm fixed | 339 Hz |
| Treble | 470 pF | 10 kOhm linear | 33.9 kHz |

SPICE simulation of the real circuit gives a response that is nothing like
flat: a **deep mid scoop of about 14-15 dB centred near 500-600 Hz**, with the
low bass sitting a couple of dB *above* the 10 kHz level. Relative to 10 kHz:
20 Hz ≈ +2.5 dB, 100 Hz ≈ −7.5 dB, 500 Hz ≈ −14.5 dB.

**A profoundly mid-scooped, extended-top curve — which is exactly the "bright,
edgy" character with a bass-guitar-like low end.**

Yamaha's own factory alignment corroborates the control ranges: bass ≈ 17 dB at
50 Hz, treble ≈ 12.5 dB at 5 kHz. The CP-80's brilliance switch measures
+5.5 / 0 / −12.0 dB at 5 kHz. **The CP-70 and CP-70B have no brilliance
switch** — CP-80 only.

## Pickup summing differs between the models

Not documented anywhere except the schematics:

- **CP-80**: all 88 elements in parallel on one bus, with a **0.022 uF mixing
  capacitor in series between key 82 and key 83**, forming a capacitive divider
  that trims the top six notes.
- **CP-70 / 70B**: elements in **six blocks**, with two **4.7 kOhm mixing
  resistors** in the bus between keys 63/64 and 65/66.

**Piezo ceramic type, element dimensions and self-capacitance: NOT FOUND.**
This matters, because the input high-pass corner depends on it: 470 kOhm with
100 nF gives 3.39 Hz for an ideal voltage source, but the real source is a
charge source with its own capacitance in series, so the true corner is **at
least 3.39 Hz and rises toward 6.8 Hz or beyond** depending on the unknown.

## Tremolo is an auto-panner

**0.8 to 10 Hz**, continuously variable; depth more than 40% max, less than 15%
min. Two LDR optocouplers shunt the two audio channels, driven in **antiphase**
— the oscillator goes direct to one and through a unity-gain inverter to the
other. Yamaha's own words: *"Two 600-ohm, balanced, transformer-isolated XLR
output jacks (one for each phase of the Tremolo)"*, and the CP-70M block diagram
labels the two modulators with phase and inverted-phase symbols.

Same family as the Rhodes suitcase vibrato: amplitude modulation in opposition
across two channels, heard as panning rather than tremolo.

Outputs: two balanced XLR at −20 dBm / 600 ohm, transformer-isolated, plus two
unbalanced jacks and a patch loop. The CP-70M/80M add a 7-band graphic EQ at
100 Hz to 6.4 kHz, ±12 dB.

## Not found

Shortest treble string length; per-string and total frame tension; frame casting
material; piezo ceramic type, dimensions and capacitance; any measured
inharmonicity coefficient for a CP; any T60 or decay measurement; any published
spectrogram; hammer strike-point ratio.

The physical argument that removing the soundboard lengthens the decay is sound
— it removes the dominant radiation loss — but **nobody has published a
measurement of it on this instrument**, so it should not be stated as fact.

The same goes for unison coupling: on an acoustic piano the soundboard couples
the strings of a unison and produces Weinreich's prompt-and-aftersound double
decay. On the CP both strings of a bichord terminate on the same rigid piezo
block bolted to a casting, which is a far higher-impedance and lower-loss
termination. **No source discusses this for the CP — treat it as an open
question, not a conclusion.**

## Sources

- CP-80 Service Manual (1978), 52 pp + fold-outs — https://manuals.fdiskc.com/tree/Yamaha/Yamaha%20CP-80%20Service%20Manual.zip
- Electric Grand Parts List, CP-70 / CP-70B / CP-80 — https://manuals.fdiskc.com/tree/Yamaha/Yamaha%20Electric%20Grand%20Parts%20List%20for%20CP-70%20CP-70B%20CP-80.pdf
- CP-70B Operating Manual, with full specs and overall circuit — https://synthfool.com/docs/Yamaha/Yamaha%20CP-70B%20Operating%20Manual.pdf
- CP-70M / CP-80M Owner's Manual, from Yamaha — https://data.yamaha.com/files/download/other_assets/8/322078/CP80ME.PDF
- Redrawn CP-80 preamp schematic and SPICE study — https://github.com/tjscientist/Yamaha-Preamp-Work
- Modartt, on physically modelling a CP-80 — https://www.modartt.com/cp-80
- Lenhoff & Robertson, *Classic Keys*, Univ. of North Texas Press 2019, pp. 326-334

**Wrong in a widely-cited secondary source:** curiocomp.com states the CP-70
covered "F1 to E7", which is 72 notes. Arturia's manual says electro-acoustic
pianos use "magnetic pickups" — the CP does not.
