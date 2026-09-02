# Epi control map

Every parameter Epi has, reachable over MIDI, in both directions.

This exists so that Epi can have a physical front panel. `epi-headless` runs the
instrument on a small computer with no screen — a board inside a keyboard, a
rack box, a Pi on a stage — and a box of encoders, buttons and displays talks to
it over an ordinary MIDI cable. Because the map works in both directions, that
panel stays correct: load a preset from the web interface and the encoders'
displays follow, rather than showing whatever they were left at.

The same numbers work from a sequencer, a controller template, or anything else
that speaks MIDI.

---

## The two channels

| | Resolution | Messages | Covers |
| --- | --- | --- | --- |
| **CC** | 7-bit, 128 steps | 1 | 49 of 49 parameters |
| **NRPN** | 14-bit, 16384 steps | 4 | 49 of 49 parameters |

**CC** is what a generic controller sends and what a sequencer lane records. One
message, no state, and every parameter has one.

**NRPN** is the precise path. 16384 steps is finer than Epi's own knobs resolve,
so an encoder can sweep a parameter without stepping audibly. Use it for
anything continuous that a listener will hear moving — `pickupPos`, `coilFreq`,
`tremRate`, `bodySize`.

Both carry the parameter's **normalised value**: the same 0 to 1 that the
interface and a host's automation lane use. So `outGain` at 0 is its minimum
(−24 dB), not "zero decibels", and a five-way selector is addressed as
`index / (choices − 1)`. One convention for every parameter, and the round trip
is exact in both directions.

---

## Sending a value

### As a CC

```
Bn cc value          n = channel, value = round(normalised x 127)
```

`outGain` is CC 7, so `B0 07 64` sets the output to 100/127 of its range.

### As an NRPN

Four messages, in this order:

```
Bn 63 00             CC 99  — NRPN bank (always 0)
Bn 62 nn             CC 98  — NRPN number, from the tables below
Bn 06 mm             CC 6   — data MSB
Bn 26 ll             CC 38  — data LSB
```

The value is `(mm << 7) | ll`, so `normalised = value / 16383`.

`outGain` is NRPN 85. To set it to 0.784:

```
B0 63 00   B0 62 55   B0 06 64   B0 26 2A        0.784 x 16383 = 12842
```

**A controller that sends no data LSB still works.** The value is applied when
the MSB arrives, with a zero low byte, and applied again if a LSB follows. The
intermediate is at most one part in 128 from the final, which no parameter here
turns into an audible step.

**CC 96 and CC 97** (data increment and decrement) move the selected NRPN by one
CC step — the reading that makes a front-panel button useful. The specification
leaves the amount to the instrument.

### Selectors

A selector with *n* choices is addressed as `index / (n − 1)`. Rounding is to
nearest in both directions, so an index survives the round trip exactly for up
to 128 choices over CC and far more over NRPN; Epi's largest selector has six.

To choose `Grand` (index 3 of the five instruments): `3 / 4 = 0.75`, so CC 90
takes 95, or NRPN 0 takes 12287.

---

## Receiving

Point `epi-headless --midi-out` at a port and it reports every parameter change
back, whatever caused it — a preset load, the web interface, a host, another
controller. That is what lets a panel with displays stay truthful.

**On connection** every parameter is stated once, so a controller plugged in
after the fact is caught up rather than left guessing.

**Then only changes are sent.** A still instrument puts nothing on the wire.

**Feedback is paced.** At most 8 parameters are reported per tick at 30 ticks a
second. A preset load moves all 49 at once, which as NRPN is nearly 200
messages; a DIN port carries about a thousand a second, so sending them in one
burst would block the port for a fifth of a second and delay the notes behind
them. Paced, the whole panel refreshes in under a fifth of a second and the port
is never occupied.

**Your own edits are not echoed back.** A value that arrives over MIDI is
recorded as already known.

**One correction may follow an edit, if it is large.** The instrument will not
hold an incoming value exactly — a selector snaps to an index, a continuous
parameter to its own step. Measured on the real parameters those corrections run
between 9 and 64 parts in 16384, all inside a single CC step, and a correction
that small is absorbed silently: the sender cannot represent or display it, so
reporting it would only put a message on the wire for every message taken off
it. A correction *larger* than a CC step means the instrument went somewhere the
sender did not ask for, and that is always reported.

Pass `--no-cc-feedback` to send only NRPN, which halves the return traffic if
your panel reads the 14-bit values anyway.

---

## Controllers Epi does not map

Deliberately left alone, because the protocol or the instrument already owns
them:

| Controller | Why |
| --: | --- |
| 0, 32 | Bank select |
| 1 | Modulation — left free for the host to map |
| 6, 38 | Data entry, used by NRPN and RPN |
| 11 | Expression, read by the engine |
| 64 | Sustain — read continuously, not as a switch, so half-pedalling works |
| 66 | Sostenuto |
| 67 | Soft pedal |
| 96, 97 | Data increment and decrement |
| 98, 99 | NRPN select |
| 100, 101 | RPN select, read by the MPE tuner |
| 120–127 | Channel mode |
| 14, 15 | Unassigned on purpose, kept as spares |

CC 0–31 have low-order counterparts at CC 32–63. Epi does not read them: the
14-bit path is NRPN, uniformly, rather than 14-bit for the twelve parameters
that happen to sit below CC 32 and 7-bit for the rest.

Where a controller number has a real meaning in the MIDI specification and that
meaning matches the parameter, the standard number is used — CC 7 is Channel
Volume and Epi's output level *is* a channel volume. Where it does not match, a
number from the undefined ranges is used instead. Those choices are noted in the
tables.

---

## MIDI 2.0

Everything above is MIDI 1.0 and stays true. This is what a MIDI 2.0 endpoint
adds, and where each part goes.

`epi-headless --list-devices` reports the endpoints it can see and what each
one can do; `--ump-in <name>` opens one. An endpoint that only speaks MIDI 1.0
is opened as MIDI 1.0 and still works — the same decoder reads it, because a
MIDI 1.0 message on a UMP transport is still a UMP.

### Velocity, at sixteen bits

A MIDI 2.0 note-on carries velocity in sixteen bits: 65536 steps instead of
128. Across the useful playing range 7-bit velocity is about a third of a
decibel per step, which is coarse for a hammer and much coarser than a keybed
that fits a velocity to a measured trajectory can resolve.

**A MIDI 2.0 note-on with velocity zero is a note ON**, at the quietest
playable level — not a release. The running-status trick that made velocity
zero a note-off does not exist in a format with an explicit note-off.

### A note's exact pitch, in the note-on

Attribute type 3 carries the note's pitch as 7.9 fixed point: a note number
and nine bits of fraction. Epi reads it as the per-note tuning offset it
already latches at the strike and never revisits — a tuner's instruction for
that string, not a bend.

| Attribute | Meaning |
| --: | --- |
| 3 | Pitch 7.9. Becomes the note's tuning in cents. |
| anything else | Ignored; the note plays at its nominal pitch. |

### Continuous key position

The one that changes what the instrument can do. A key on a grand lifts its
own damper as it descends and lets it back on the way up, which is why
half-releasing a key half-damps the note. Two contacts cannot say that.

| Per-note controller | Index | Meaning |
| --- | --: | --- |
| **Assignable** | **1** | Key position. 0 at rest, 1 fully depressed. |

Assignable, not registered — the registered controllers have assigned meanings
and none of them is "where the key is". Registered controller 1 is Modulation
and is deliberately not read as key position.

The damper here is a contact damping term rather than an envelope, so the
number has somewhere real to go. Measured: a key left down keeps its damper off
the string by 52.6 dB against a released one, a key held halfway lands 35.1 dB
between the two, and the response is monotone across the travel. The travel is
mapped to lift between 0.35 and 0.85 of full dip — the damper starts to rise
partway down and is clear before let-off. That mapping is the one number here
that is **not measured**, and is recorded as a gap rather than dressed up.

### Controllers, at thirty-two bits

A MIDI 2.0 Assignable Controller is the 32-bit successor to NRPN and uses the
same numbers as the NRPN column in the tables above — bank 0, the published
index — with 4294967296 steps instead of 16384. Control Change is likewise
32-bit and uses the same CC numbers.

### Jitter reduction

Almost nothing implements JR timestamps. Epi reads them.

The arrival time of a MIDI message says more about transport scheduling than
about when the key was pressed. A JR Timestamp carries when the event actually
happened, so the quantisation the transport imposed can be undone.

Measured, with notes sent exactly 10 ms apart and up to 4 ms of random lateness
added by the transport:

| | |
| --- | --- |
| as delivered | 3.55 ms of smear between gaps |
| reconstructed | **1 sample** — 0.021 ms at 48 kHz |
| improvement | **171×** |

Two details that matter for a sender:

**Emit JR Clock continuously**, several times a second, whether or not
anything is being played. Mapping your clock onto ours means finding the
offset between them, and the estimator tracks the smallest arrival-minus-
timestamp it has seen — every message is late by some amount and none is
early. That estimate is causal, so it has a transient: with a clock already
running it is locked before the first note, and without one the first few notes
pay for the lock (measured at 3.04 ms worst, exact from about the eighth note).

**A JR Timestamp may be earlier than the last JR Clock.** That is normal — it
says when a past event happened. Epi does not read a small step backwards as
the 16-bit counter wrapping.

### Where this works

| Host | MIDI 2.0 |
| --- | --- |
| `epi-headless` | Yes — CoreMIDI on macOS 11+, ALSA UMP on Linux 6.5+ |
| Plugin | Whatever the host provides; VST3 and CLAP have no UMP path today |
| Browser build | No. Web MIDI is MIDI 1.0 only |

---

## These numbers will not move

`src/epi/ControlMap.h` is a published interface. The numbers are written out
explicitly rather than derived from the parameter list's order, so adding a
parameter takes the next free number instead of renumbering everything after it,
and `tests/test_epi_control.cpp` holds an independent copy of every assignment.
Changing one fails a test. A number can therefore only move as a decision, never
as a side effect.

New parameters are added at the end of their panel's reserved block.

---

## The map

<!-- BEGIN GENERATED — do not edit by hand; run tools/update-control-map.sh -->

### Instrument

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 90 | 0 | **Instrument** `instrument` | Tine / E-Grand / Reed / Grand / Clav | Tine | which of the five instruments |
| 28 | 1 | **Tune** `tune` | -100.00 .. 100.00 | 0.00 |  |
| 3 | 2 | **Clarity** `clarity` | -12.00 .. 12.00 | 0.00 |  |

### Action

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 20 | 8 | **Velocity Curve** `velCurve` | 0.00 .. 1.00 | 0.50 |  |
| 73 | 9 | **Hammer** `hammerHard` | 0.00 .. 1.00 | 0.50 | Sound Controller 4, Attack Time |
| 21 | 10 | **Hammer Mass** `hammerMass` | 0.00 .. 1.00 | 0.50 |  |
| 22 | 11 | **Escapement** `escapement` | 0.00 .. 1.00 | 0.40 |  |
| 23 | 12 | **Key Noise** `strikeNoise` | 0.00 .. 1.00 | 0.22 |  |
| 75 | 13 | **Damper** `damperGrip` | 0.00 .. 1.00 | 0.60 | Sound Controller 6, Decay Time |
| 24 | 14 | **Key Bed** `keyBed` | Stock / Fresh Felt / Leather / Worn | Stock |  |
| 25 | 15 | **Hammer Covering** `hammerMat` | Stock / Soft Felt / Hard Felt / Lacquered / Leather / Wood | Stock |  |
| 26 | 16 | **Damper Felt** `damperFelt` | Stock / Fresh / Worn / Hardened | Stock |  |
| 27 | 17 | **Soft Pedal** `softMode` | Shift / Rail | Shift |  |

### Resonator

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 70 | 24 | **Tuning Spring** `tipMass` | 0.00 .. 1.00 | 0.50 | Sound Controller 1, Sound Variation |
| 72 | 25 | **Damping** `resDamp` | 0.00 .. 1.00 | 0.35 | Sound Controller 3, Release Time |
| 29 | 26 | **Tone Bar** `barCouple` | 0.00 .. 1.00 | 0.60 |  |
| 30 | 27 | **Bar Tune** `barTune` | -24.00 .. 24.00 | 0.00 |  |
| 85 | 28 | **Bloom** `nonlinAmt` | 0.00 .. 1.00 | 0.50 |  |
| 86 | 29 | **Material** `material` | Music Wire / Stainless / Bronze / Brass / Titanium / Aluminium / Tungsten / Nylon | Music Wire |  |
| 89 | 30 | **Wear** `wearAmount` | 0.00 .. 1.00 | 0.00 |  |

### Body

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 31 | 36 | **Body** `bodyMix` | 0.00 .. 1.00 | 0.25 |  |
| 87 | 37 | **Body Material** `bodyMat` | Stock / Spruce / Maple / Birch Ply / Aluminium / Steel / Brass / Carbon | Stock |  |
| 88 | 38 | **Body Size** `bodySize` | 0.00 .. 1.00 | 0.50 |  |

### Pickup

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 102 | 44 | **Pickup Height** `pickupPos` | -1.00 .. 1.00 | -0.35 |  |
| 103 | 45 | **Pickup Gap** `pickupDist` | 0.00 .. 1.00 | 0.35 |  |
| 106 | 46 | **Pickup** `pickupSel` | Magnetic / Native / Electro / Contact | Native |  |
| 107 | 47 | **Clav Pickup** `clavSwitch` | Center / Bridge / Both / Out of Phase | Center |  |
| 104 | 48 | **Coil Peak** `coilFreq` | 0.00 .. 1.00 | 0.50 |  |
| 105 | 49 | **Coil Q** `coilQ` | 0.00 .. 1.00 | 0.50 |  |
| 71 | 50 | **Core Sat** `coilSat` | 0.00 .. 1.00 | 0.25 | Sound Controller 2, Harmonic Intensity |

### Amplifier

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 108 | 56 | **Drive** `preampDrive` | 0.00 .. 1.00 | 0.30 |  |
| 109 | 57 | **Bass** `bass` | -12.00 .. 12.00 | 0.00 |  |
| 74 | 58 | **Treble** `treble` | -12.00 .. 12.00 | 0.00 | Sound Controller 5, Brightness |
| 111 | 59 | **Cabinet** `cabMix` | 0.00 .. 1.00 | 0.50 |  |
| 116 | 60 | **Brilliant** `clavBrill` | 0.00 .. 1.00 | 0.00 |  |
| 117 | 61 | **Treble Rocker** `clavTreb` | 0.00 .. 1.00 | 0.00 |  |
| 118 | 62 | **Medium Rocker** `clavMed` | 0.00 .. 1.00 | 1.00 |  |
| 119 | 63 | **Soft Rocker** `clavSoft` | 0.00 .. 1.00 | 0.00 |  |

### Modulation

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 76 | 70 | **Tremolo Rate** `tremRate` | 0.10 .. 12.00 | 5.50 | Vibrato Rate, the nearest standard rate control |
| 92 | 71 | **Tremolo** `tremDepth` | 0.00 .. 1.00 | 0.00 | Effects 2 Depth, defined as Tremolo Depth |
| 110 | 72 | **Trem Width** `tremStereo` | 0.00 .. 1.00 | 1.00 |  |
| 95 | 73 | **Phaser** `phaserMix` | 0.00 .. 1.00 | 0.00 | Effects 5 Depth, defined as Phaser Depth |
| 112 | 74 | **Phaser Rate** `phaserRate` | 0.02 .. 8.00 | 0.40 |  |
| 113 | 75 | **Phaser Depth** `phaserDepth` | 0.00 .. 1.00 | 0.70 |  |
| 114 | 76 | **Phaser Res** `phaserFb` | 0.00 .. 1.00 | 0.50 |  |

### Output

| CC | NRPN | Parameter | Range | Default | Note |
| --: | --: | --- | --- | --- | --- |
| 91 | 82 | **Space** `spaceMix` | 0.00 .. 1.00 | 0.15 | Effects 1 Depth, defined as Reverb Send |
| 115 | 83 | **Space Size** `spaceSize` | 0.00 .. 1.00 | 0.40 |  |
| 9 | 84 | **Room** `roomProfile` | Custom / Booth / Studio / Stage / Hall / Church | Custom |  |
| 7 | 85 | **Output** `outGain` | -24.00 .. 12.00 | 0.00 | Channel Volume |

<!-- END GENERATED -->

---

## Worked example: a four-encoder panel

Four encoders and a display, on MIDI channel 1, for the pickup section:

| Encoder | Parameter | NRPN | Send | Receive |
| --- | --- | --: | --- | --- |
| 1 | Pickup height | 44 | `B0 63 00  B0 62 2C  B0 06 mm  B0 26 ll` | same, unsolicited |
| 2 | Pickup gap | 45 | `... B0 62 2D ...` | same |
| 3 | Coil frequency | 48 | `... B0 62 30 ...` | same |
| 4 | Coil saturation | 50 | `... B0 62 32 ...` | same |

Start `epi-headless --midi-in "<your panel>" --midi-out "<your panel>"`. On
connection it states all 49 parameters; keep the four you care about and ignore
the rest. Then send on turn, and redraw on receive.
