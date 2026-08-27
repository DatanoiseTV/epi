/*
  Epi — physically modeled electric pianos
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

// The factory bank as data, free of any framework so the preset verification
// harness can render every entry through the same engine the plugin runs.
// FactoryPresets.cpp adapts this into the PresetManager's shape.
//
// Ground rules, learned the hard way and enforced by tests/test_epi_presets.cpp:
//
//  - "Suitcase" IS the reference: the voicing the acoustic suite verifies
//    against a real 1977 Mark I. Its values do not change, ever, without
//    re-running that suite. Every other preset is a deliberate departure and
//    its comment says from what.
//  - The tine pickup gap stays at or above knob 0.20 (about 1.4 mm). Below
//    that the tine leaves the pole face twice a cycle: the inharmonic floor
//    comes up 45 dB, the fundamental beats against itself, the attack
//    collapses to a click. Dirt comes from the DRIVE and the core
//    saturation, which is where the instrument gets it.
//  - Transducers and materials pair by physics, not by taste: materials 2-7
//    (everything after stainless) are invisible to a MAGNETIC pickup, and
//    nylon (7) cannot be the moving plate of an ELECTROSTATIC one. A preset
//    that ignores this is silent, and the harness fails silent presets.
//  - A preset stores only what departs from the parameter defaults; the
//    adapter completes it so what ships is still a full, self-contained
//    voicing.
namespace epi::presetdata
{

struct ParamValue { const char* id; float value; };

struct Preset
{
    const char* name;
    int         instrument;   // 0 Tine, 1 E-Grand, 2 Reed, 3 Grand
    const ParamValue* values; // departures from the defaults below
    std::size_t numValues;
};

// The parameter defaults, in real units, mirroring the APVTS layout in
// epi/ParameterIDs.h. The harness cross-checks every entry against its own
// replica of that layout, so drift between the two fails the suite.
inline constexpr ParamValue kDefaults[] = {
    { "instrument", 0.0f },  { "tune", 0.0f },
    { "velCurve", 0.5f },    { "hammerHard", 0.5f },  { "hammerMass", 0.5f },
    { "escapement", 0.4f },  { "strikeNoise", 0.22f },{ "damperGrip", 0.6f },
    { "tipMass", 0.5f },     { "resDamp", 0.35f },
    { "barCouple", 0.6f },   { "barTune", 0.0f },
    { "bodyMix", 0.25f },    { "nonlinAmt", 0.5f },
    { "pickupPos", -0.35f }, { "pickupDist", 0.35f }, { "pickupSel", 1.0f },
    { "coilFreq", 0.5f },    { "coilQ", 0.5f },       { "coilSat", 0.25f },
    { "preampDrive", 0.3f }, { "bass", 0.0f },        { "treble", 0.0f },
    { "clarity", 0.0f },     { "material", 0.0f },
    { "tremRate", 5.5f },    { "tremDepth", 0.0f },   { "tremStereo", 1.0f },
    { "cabMix", 0.5f },
    { "phaserMix", 0.0f },   { "phaserRate", 0.40f },
    { "phaserDepth", 0.70f },{ "phaserFb", 0.50f },
    { "spaceMix", 0.15f },   { "spaceSize", 0.40f },
    { "outGain", 0.0f },
    { "clavSwitch", 0.0f },  { "clavBrill", 0.0f },   { "clavTreb", 0.0f },
    { "clavMed", 1.0f },     { "clavSoft", 0.0f },
    { "bodyMat", 0.0f },     { "bodySize", 0.5f },
    { "damperFelt", 0.0f },  { "keyBed", 0.0f },  { "hammerMat", 0.0f },  { "roomProfile", 0.0f },  { "softMode", 0.0f },  { "wearAmount", 0.0f },
};

// ---------------------------------------------------------------------------
// Tine (Rhodes). "Suitcase" is the frozen reference; the rest depart from it
// the way real instruments depart: voicing screws, hammers, amps, transducers.
// ---------------------------------------------------------------------------

// The instrument the suite verifies: a well-maintained Mark I into its own
// Suitcase amplifier, panner running gently. Change nothing here without
// re-running the acoustic suite -- this preset is its baseline.
inline constexpr ParamValue kSuitcase[] = {
    { "tremDepth", 0.35f }, { "tremRate", 4.2f },
    { "bass", 2.0f }, { "treble", 1.0f }, { "cabMix", 0.55f },
    { "spaceMix", 0.12f },
};

// The same harp DI'd through a bright amp: no panner, less cabinet, a little
// more edge. What most records of a Stage actually are.
inline constexpr ParamValue kStageDI[] = {
    { "cabMix", 0.30f }, { "treble", 2.5f },
    { "preampDrive", 0.35f }, { "spaceMix", 0.08f },
};

// Voiced dark: pickup low and far, soft tip, felt loose. The gap stays
// legal -- the softness comes from where softness comes from on the
// instrument, the voicing screw and the hammer.
inline constexpr ParamValue kMellow[] = {
    { "pickupPos", -0.70f }, { "pickupDist", 0.45f },
    { "hammerHard", 0.28f }, { "resDamp", 0.42f },
    { "coilFreq", 0.32f }, { "coilQ", 0.38f },
    { "preampDrive", 0.18f }, { "bass", 3.0f }, { "treble", -2.0f },
    { "tremDepth", 0.25f }, { "tremRate", 3.2f }, { "cabMix", 0.6f },
    { "spaceMix", 0.12f },
};

// The tape-era ballad setting: long sustain (bar fully coupled), soft touch,
// wide slow pan, a real room. Freshly serviced action -- new damper felt and
// new rail cloth -- because the quiet, even mechanism IS the ballad
// instrument; before the action bench this could only be approximated by
// leaving the noise knob down.
inline constexpr ParamValue kBallad[] = {
    { "barCouple", 0.9f }, { "hammerHard", 0.32f }, { "velCurve", 0.38f },
    { "resDamp", 0.18f }, { "pickupPos", -0.5f },
    { "damperFelt", 1.0f }, { "keyBed", 1.0f },
    { "preampDrive", 0.15f }, { "bass", 2.0f },
    { "tremDepth", 0.30f }, { "tremRate", 2.6f },
    { "spaceMix", 0.30f }, { "spaceSize", 0.60f },
};

// Bright and percussive: hard tip, tight damper, bar coupling backed off so
// the notes get out of each other's way, no modulation.
inline constexpr ParamValue kFunk[] = {
    { "hammerHard", 0.80f }, { "velCurve", 0.65f },
    { "damperGrip", 0.85f }, { "barCouple", 0.42f }, { "resDamp", 0.5f },
    { "pickupPos", -0.22f },
    { "coilFreq", 0.68f }, { "coilQ", 0.66f },
    { "preampDrive", 0.45f }, { "treble", 3.5f },
    { "cabMix", 0.45f }, { "spaceMix", 0.06f },
};

// Driven hard, the honest way: the gap is LEGAL, and the dirt is the preamp
// run into its knee plus the cores into saturation -- which is what an
// overdriven Rhodes is.
inline constexpr ParamValue kBark[] = {
    { "pickupDist", 0.25f }, { "pickupPos", -0.30f },
    { "hammerHard", 0.70f }, { "hammerMass", 0.62f },
    { "coilSat", 0.55f }, { "preampDrive", 0.78f },
    { "bass", 3.0f }, { "treble", 1.5f }, { "cabMix", 0.7f },
    { "spaceMix", 0.05f },
};

// Voiced on the pole centreline: the tine crosses the field peak twice a
// cycle and the note comes out an octave up with almost no fundamental. A
// real setting -- the reason a badly voiced Rhodes sounds thin -- kept
// because it teaches what the voicing screw does.
inline constexpr ParamValue kBell[] = {
    { "pickupPos", -0.05f }, { "pickupDist", 0.32f },
    { "hammerHard", 0.6f }, { "barCouple", 0.8f },
    { "coilFreq", 0.7f }, { "coilQ", 0.6f },
    { "preampDrive", 0.2f }, { "treble", 3.0f },
    { "cabMix", 0.35f }, { "spaceMix", 0.18f },
};

// The Wurlitzer's kind of tremolo on the Rhodes' tone: photocells wired
// TOGETHER, so it pulses instead of panning.
inline constexpr ParamValue kAmpTremolo[] = {
    { "tremDepth", 0.55f }, { "tremRate", 5.8f }, { "tremStereo", 0.0f },
    { "preampDrive", 0.35f }, { "cabMix", 0.6f },
    { "spaceMix", 0.12f },
};

// A Rhodes through a slow phaser, which is one of the two or three sounds
// the instrument is known for.
inline constexpr ParamValue kPhase90[] = {
    { "phaserMix", 0.55f }, { "phaserRate", 0.35f },
    { "phaserDepth", 0.80f }, { "phaserFb", 0.62f },
};

// Contributed from the bench: a hot-driven suitcase voicing with the coil
// peak pulled dark and the core saturation almost off -- the dirt comes from
// the preamp alone, which reads as breath rather than fuzz.
inline constexpr ParamValue kSylwester[] = {
    { "pickupPos", -0.424f }, { "pickupDist", 0.331f },
    { "coilFreq", 0.281f }, { "coilSat", 0.037f },
    { "preampDrive", 0.802f }, { "bass", 2.0f }, { "treble", 1.0f },
    { "tremDepth", 0.35f }, { "tremRate", 4.2f },
    { "cabMix", 0.55f }, { "spaceMix", 0.12f },
};

// The transducer swap the project was founded on: the same tines read by a
// polarised capacitor instead of a coil. Displacement, not velocity -- the
// fundamental comes forward and the top goes glassy. Departs from Suitcase
// only at the transducer and the trim that rebalances it.
inline constexpr ParamValue kElectroTine[] = {
    { "pickupSel", 2.0f },
    { "tremDepth", 0.30f }, { "tremRate", 4.2f },
    { "cabMix", 0.40f }, { "spaceMix", 0.12f },
    { "outGain", 5.0f },
};

// The same tines read at the mount: the force on the frame, every partial
// weighted by what it delivers to the clamp. Woody, acoustic-adjacent, no
// magnet voicing at all. More body, less cabinet.
inline constexpr ParamValue kContactTine[] = {
    { "pickupSel", 3.0f },
    { "bodyMix", 0.40f }, { "cabMix", 0.25f },
    { "spaceMix", 0.18f }, { "spaceSize", 0.50f },
    { "outGain", 6.0f },
};

// Bronze tines through the contact transducer. Bronze is invisible to a coil
// (not ferromagnetic), so this pairing is the only honest way to hear it:
// heavier, softer metal, more internal loss -- a gamelan-adjacent decay.
inline constexpr ParamValue kBronzeBars[] = {
    { "material", 2.0f }, { "pickupSel", 3.0f },
    { "barCouple", 0.75f }, { "resDamp", 0.30f },
    { "bodyMix", 0.35f }, { "cabMix", 0.30f },
    { "spaceMix", 0.22f }, { "spaceSize", 0.55f },
    { "outGain", 6.0f },
};

// Aluminium tines through the contact transducer -- the only transducer that
// hears them at full strength. A third the density of steel: the same strike
// swings the tine further and the internal loss is the highest of the
// metals, so the note blooms hard and dies young. Departs from Contact Tine
// at the material.
inline constexpr ParamValue kAlloyTines[] = {
    { "material", 5.0f }, { "pickupSel", 3.0f },
    { "barCouple", 0.70f }, { "resDamp", 0.25f },
    { "bodyMix", 0.35f }, { "cabMix", 0.25f },
    { "spaceMix", 0.18f }, { "spaceSize", 0.50f },
    // Re-levelled when the contact pickup stopped over-weighting the tine's
    // overtones: each mode now carries its own generalised mass, which is
    // 1.3 to 2.3 times the fundamental's for the modes a contact pickup
    // hears best, and this patch rode the old louder version.
    { "outGain", 8.5f },
};

// The instrument that has done two hundred gigs and missed every service:
// worn damper felt, the rail cloth worn through, every tine drifted a few
// cents its own way (workshop table below). The aged sibling of Stage DI.
// The worn cloth's release thud is real and measured -- the harness isolates
// it against this preset's own noise-free twin, at the thud's own level,
// because a full mf chord masks a key-up thump exactly as it does on the
// real instrument.
inline constexpr ParamValue kTiredStage[] = {
    { "hammerHard", 0.55f }, { "strikeNoise", 0.32f },
    { "damperFelt", 2.0f }, { "keyBed", 3.0f },
    { "resDamp", 0.42f },
    { "preampDrive", 0.34f }, { "treble", 1.0f }, { "bass", 1.0f },
    { "cabMix", 0.55f }, { "spaceMix", 0.10f },
};

// ---- Tine workshop presets ------------------------------------------------
// These four re-cut the harp itself through the tine workshop (tables in
// this header, applied by name from FactoryPresets.cpp). Loading one paints
// its table; loading any other preset leaves the workshop alone, so a
// hand-painted scale survives browsing the amp voicings.

// Five-limit just intonation in C on the ballad voicing. The point of just
// thirds on THIS instrument: held chords stop beating, so the bar coupling's
// long sustain and the bloom breathe instead of churn.
inline constexpr ParamValue kJustBallad[] = {
    { "barCouple", 0.9f }, { "hammerHard", 0.30f }, { "velCurve", 0.38f },
    { "resDamp", 0.18f }, { "pickupPos", -0.5f },
    { "damperFelt", 1.0f }, { "keyBed", 1.0f },
    { "preampDrive", 0.15f }, { "bass", 2.0f },
    { "tremDepth", 0.25f }, { "tremRate", 2.4f },
    { "spaceMix", 0.32f }, { "spaceSize", 0.62f },
};

// The keyboard mapped onto a five-tone slendro: neighbouring keys collapse
// onto shared degrees, exactly what happens when a keyboard is mapped to a
// gamelan. The gauge lane goes fat through the middle, which drags the
// overtones flat the way a thick bar's shear does -- the gong colour is the
// geometry, not an effect.
inline constexpr ParamValue kSlendroBells[] = {
    { "hammerHard", 0.68f }, { "hammerMass", 0.58f },
    { "barCouple", 0.75f }, { "resDamp", 0.22f },
    { "coilFreq", 0.60f }, { "coilQ", 0.60f },
    { "preampDrive", 0.20f }, { "treble", 1.5f },
    { "spaceMix", 0.28f }, { "spaceSize", 0.55f }, { "cabMix", 0.35f },
    { "outGain", 4.0f },
};

// The black keys raised a quarter tone: the whites play as ever, and the
// blacks become neutral seconds and thirds -- maqam-flavoured inflections on
// an otherwise familiar keyboard.
inline constexpr ParamValue kQuarterKeys[] = {
    { "cabMix", 0.30f }, { "treble", 2.0f },
    { "preampDrive", 0.30f }, { "spaceMix", 0.10f },
};

// Every tine a few cents its own way, deterministic per note: the barroom
// upright's chorus, from mistuning rather than modulation. The action bench
// finishes the picture the noise knob used to fake alone: worn damper felt
// and worn-through rail cloth, the loud bright clack of a neglected room's
// instrument.
inline constexpr ParamValue kBarroom[] = {
    { "hammerHard", 0.62f }, { "strikeNoise", 0.35f },
    { "damperFelt", 2.0f }, { "keyBed", 3.0f },
    { "preampDrive", 0.38f }, { "treble", 2.0f }, { "bass", 1.5f },
    { "cabMix", 0.55f }, { "spaceMix", 0.14f },
};

// The benches stacked into an instrument that never existed and should:
// bronze bars on the slendro map (same table as Slendro Bells), read at the
// clamp, heavier hammers, a long room. Every choice is physics the honest
// presets already use -- non-ferrous metal through contact, the workshop's
// geometry -- they have just never been bolted together before. The frame
// bench is deliberately NOT set here: measured, the tine frame's swap sits
// over 70 dB under the notes in the mix, so claiming a "temple case" from
// it would be a label, not a sound.
inline constexpr ParamValue kGongTemple[] = {
    { "material", 2.0f }, { "pickupSel", 3.0f },
    { "hammerHard", 0.66f }, { "hammerMass", 0.58f },
    { "barCouple", 0.80f }, { "resDamp", 0.20f },
    { "bodyMix", 0.40f }, { "cabMix", 0.25f },
    { "spaceMix", 0.30f }, { "spaceSize", 0.65f },
    // Two decibels under where it was: fat wire and a long decay stack more
    // energy than any other tine patch, and it was the last one still
    // grazing the output rail after the bank moved to a peak bench.
    { "outGain", 4.0f },
};

// ---------------------------------------------------------------------------
// E-Grand (CP-70). Strings on a piezo bridge through the mid-scooped preamp;
// everything magnetic is inert on the native transducer by construction.
// ---------------------------------------------------------------------------

// The electric grand reference: piezo bridge, antiphase panner running, no
// cabinet -- the DI sound of the records.
inline constexpr ParamValue kCP70[] = {
    { "instrument", 1.0f },
    { "tremDepth", 0.30f }, { "tremRate", 4.5f },
    { "cabMix", 0.0f },
    { "spaceMix", 0.18f }, { "spaceSize", 0.5f },
};

// The CP driven at pop sessions: harder hammers, the top shelves up, dry and
// forward. Departs from CP-70 at the hammer and the tone stack.
inline constexpr ParamValue kBrightPop[] = {
    { "instrument", 1.0f },
    { "hammerHard", 0.68f }, { "treble", 3.0f }, { "clarity", 3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.14f },
};

// The CP as a ballad machine: soft hammers, the top rolled off, a slow
// shallow pan and a bigger room. Departs from CP-70 at hammer, tone, space --
// and, since the action bench, at the felt: fresh dampers and fresh rail
// cloth, the quiet even mechanism a ballad take gets serviced for.
inline constexpr ParamValue kDarkBallad[] = {
    { "instrument", 1.0f },
    { "hammerHard", 0.30f }, { "velCurve", 0.40f },
    { "damperFelt", 1.0f }, { "keyBed", 1.0f },
    { "bass", 2.0f }, { "treble", -2.5f },
    { "tremDepth", 0.25f }, { "tremRate", 3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.28f }, { "spaceSize", 0.60f },
};

// Tack-piano flavour: the hardest hammer the action offers plus the
// mechanism up in the mix. What a thumbtacked upright does, on strings --
// including the dampers: an instrument nobody tacks has fresh felt, so the
// hardened felt's post-release zing belongs here too.
inline constexpr ParamValue kTackGrand[] = {
    { "instrument", 1.0f },
    { "hammerHard", 0.92f }, { "strikeNoise", 0.40f }, { "escapement", 0.55f },
    { "damperFelt", 3.0f },
    { "treble", 1.5f },
    { "cabMix", 0.0f }, { "spaceMix", 0.10f },
};

// The strings read by a magnetic point pickup instead of the bridge: an
// electric guitar's view of a piano. Legal because the stock strings are
// music wire -- ferromagnetic; swap the material away from steel here and
// the pickup goes deaf. Departs from CP-70 at the transducer.
inline constexpr ParamValue kMagneticGrand[] = {
    { "instrument", 1.0f }, { "pickupSel", 0.0f },
    { "pickupPos", -0.20f }, { "pickupDist", 0.35f },
    { "coilQ", 0.55f },
    { "cabMix", 0.30f }, { "spaceMix", 0.14f },
};

// The strings as the moving plate of an electrostatic pickup: displacement
// read, glassy top. Departs from CP-70 at the transducer.
inline constexpr ParamValue kElectroGrand[] = {
    { "instrument", 1.0f }, { "pickupSel", 2.0f },
    { "treble", 1.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.16f },
    // The electrostatic pickup on wound strings is the hottest transducer
    // path in the instrument -- its transient peaked nearly three times
    // the rail knee before the bank moved to a peak bench, and this keeps
    // the last of it clear.
    { "outGain", -7.0f },
};

// The chorus-era CP: the phaser slow and shallow, which on this instrument
// reads as the rack chorus every late-seventies stage ran.
inline constexpr ParamValue kChorusEra[] = {
    { "instrument", 1.0f },
    { "phaserMix", 0.50f }, { "phaserRate", 0.15f },
    { "phaserDepth", 0.70f }, { "phaserFb", 0.40f },
    { "cabMix", 0.0f }, { "spaceMix", 0.20f }, { "spaceSize", 0.50f },
};

// Pedal-wash ambient: soft hammers, the case resonance up, the room most of
// the way open. Made to be played with the pedal down. The body bench gives
// the wash a real case to ring in: the frame scaled up, so the sympathetic
// bed sits deeper and slower than the stock casting.
inline constexpr ParamValue kPedalWash[] = {
    { "instrument", 1.0f },
    { "hammerHard", 0.35f }, { "velCurve", 0.40f },
    { "bodyMix", 0.60f }, { "bodySize", 0.75f },
    { "tremDepth", 0.20f }, { "tremRate", 3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.45f }, { "spaceSize", 0.85f },
    { "outGain", -6.0f },
};

// The dry studio machine: the case swapped for a steel frame, which barely
// moves -- the sympathetic wash drops away and what is left is the strings
// and the bridge, nothing ringing behind them. Departs from CP-70 at the
// frame and the shut room.
inline constexpr ParamValue kStudioFrame[] = {
    { "instrument", 1.0f },
    { "bodyMat", 5.0f },
    { "clarity", 1.5f },
    { "cabMix", 0.0f }, { "spaceMix", 0.08f }, { "spaceSize", 0.30f },
};

// Tungsten strings on the piezo bridge -- the bridge hears force, so the
// material arrives in full. Tungsten's E/rho is below steel's: less bending
// stiffness per unit mass, so the low strings stretch their partials less --
// a tighter, more harmonic bass than music wire can give at this scale.
// Departs from CP-70 at the material only.
inline constexpr ParamValue kTungstenWire[] = {
    { "instrument", 1.0f }, { "material", 6.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.16f },
};

// The E-Grand that toured for a decade: the damper felt hardened until the
// high partials slip through at release -- the zing, measured against this
// preset's own fresh-felt twin -- and the rail cloth worn through, kept as
// honest configuration even though a piezo bridge barely hears the key bed.
inline constexpr ParamValue kRoadworn[] = {
    { "instrument", 1.0f },
    { "damperFelt", 3.0f }, { "keyBed", 3.0f },
    { "hammerHard", 0.60f }, { "strikeNoise", 0.32f },
    { "cabMix", 0.0f }, { "spaceMix", 0.08f },
};

// ---------------------------------------------------------------------------
// Reed (Wurlitzer 200A). The cabinet bench for these is painted to the
// measured small open-back ovals by the table below.
// ---------------------------------------------------------------------------

// The reference 200A on its own speakers: supply at nominal (+150 V at the
// saturation knob's midpoint), drive at the circuit's literal level, vibrato
// at the pot's classic mid setting.
inline constexpr ParamValue kTwoHundred[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.31f }, { "coilSat", 0.5f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.17f },
    { "tremDepth", 0.5f }, { "tremRate", 5.6f },
    { "cabMix", 0.7f }, { "spaceMix", 0.10f },
    { "outGain", -6.0f },
};

// The soul-record setting: vibrato deep and classic, the drive a touch past
// the circuit's own level, played on the onboard ovals.
inline constexpr ParamValue kReedSoul[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.40f }, { "coilSat", 0.5f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.17f },
    { "tremDepth", 0.65f }, { "tremRate", 5.6f },
    { "cabMix", 0.7f }, { "spaceMix", 0.14f }, { "spaceSize", 0.45f },
    { "outGain", -6.0f },
};

// A tired unit: the rail sagging near 115 volts -- Pfeifle measured a real
// one at 130 -- which is a physical volume-and-bark drop, so the tone goes
// gentle without any tone control touching it.
inline constexpr ParamValue kSaggingRail[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.25f }, { "coilSat", 0.15f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.20f },
    { "tremDepth", 0.35f }, { "tremRate", 5.0f },
    { "cabMix", 0.65f }, { "spaceMix", 0.12f },
    { "outGain", -2.0f },
};

// The hotter Series 200 rail and the drive well into the knee: the bark
// forward, the onboard speakers grinding at their early excursion limit,
// close and dry.
inline constexpr ParamValue kReedGrind[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.60f }, { "coilSat", 0.85f },
    { "pickupPos", 0.6f }, { "pickupDist", 0.12f },
    { "tremDepth", 0.20f }, { "tremRate", 6.2f },
    { "cabMix", 0.8f }, { "spaceMix", 0.05f },
    { "outGain", -7.0f },
};

// The lead voice: drive well up but the rail at nominal, vibrato off, dry.
// Departs from Reed Grind by getting its dirt from the preamp knee rather
// than a hot rail -- bark as articulation, not fuzz.
inline constexpr ParamValue kReedBark[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.72f }, { "coilSat", 0.5f },
    { "pickupPos", 0.55f }, { "pickupDist", 0.15f },
    { "treble", 2.0f },
    { "cabMix", 0.75f }, { "spaceMix", 0.06f },
    { "outGain", -5.0f },
};

// The soft EP: felt-fresh hammers, drive backed under the circuit level, a
// gentle vibrato and a bit of room. The Sunday-morning setting -- and now
// literally felt-fresh: new damper felt and new rail cloth, the serviced
// action the name always implied.
inline constexpr ParamValue kSoftReed[] = {
    { "instrument", 2.0f },
    { "hammerHard", 0.30f }, { "velCurve", 0.40f },
    { "damperFelt", 1.0f }, { "keyBed", 1.0f },
    { "preampDrive", 0.22f }, { "coilSat", 0.4f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.22f },
    { "tremDepth", 0.30f }, { "tremRate", 4.6f },
    { "cabMix", 0.6f }, { "spaceMix", 0.18f }, { "spaceSize", 0.50f },
    { "outGain", -4.0f },
};

// The classic 200A vibrato as it actually is: one amplifier, so a true
// amplitude tremolo -- photocells together, not panned. The AM row in the
// harness measures this preset at its set rate.
inline constexpr ParamValue kReedTremolo[] = {
    { "instrument", 2.0f },
    { "preampDrive", 0.31f }, { "coilSat", 0.5f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.17f },
    { "tremDepth", 0.60f }, { "tremRate", 5.5f }, { "tremStereo", 0.0f },
    { "cabMix", 0.7f }, { "spaceMix", 0.10f },
    { "outGain", -6.0f },
};

// The reeds read at the clamp instead of electrostatically: the force at the
// mount, no polarising supply, no slot geometry. Departs from Two Hundred at
// the transducer; DI'd, so the stock cabinet stays out of it.
inline constexpr ParamValue kContactReed[] = {
    { "instrument", 2.0f }, { "pickupSel", 3.0f },
    { "preampDrive", 0.08f }, { "bass", -4.0f }, { "treble", 3.0f },
    { "cabMix", 0.30f }, { "spaceMix", 0.16f },
    { "outGain", -21.0f },
};

// Dark lo-fi: the thrift-store unit, aged where a real one ages. Before the
// action and body benches this preset was EQ and drive pretending to be
// wear; now the wear is physical -- worn damper felt gripping lazily, the
// rail cloth worn through to its clack, a smaller cheaper case -- and the
// coil peak and shelves only finish what the mechanism starts.
inline constexpr ParamValue kLoFiReed[] = {
    { "instrument", 2.0f },
    { "damperFelt", 2.0f }, { "keyBed", 3.0f },
    { "bodyMat", 3.0f }, { "bodySize", 0.30f },
    { "coilFreq", 0.30f }, { "coilQ", 0.40f },
    { "preampDrive", 0.20f }, { "coilSat", 0.30f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.20f },
    { "treble", -4.0f }, { "clarity", -5.0f },
    { "tremDepth", 0.30f }, { "tremRate", 4.0f },
    { "cabMix", 0.8f }, { "spaceMix", 0.08f },
    { "outGain", -2.0f },
};

// The body bench on the reed piano: a small birch-ply case. Light and lossy,
// it moves far more than the stock box -- the mechanical wash comes up and
// the whole instrument sounds smaller and closer, a practice unit on a desk.
// Departs from Two Hundred at the case.
inline constexpr ParamValue kPocketReed[] = {
    { "instrument", 2.0f },
    { "bodyMat", 3.0f }, { "bodySize", 0.10f }, { "bodyMix", 0.45f },
    { "preampDrive", 0.31f }, { "coilSat", 0.5f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.17f },
    { "tremDepth", 0.35f }, { "tremRate", 5.6f },
    { "cabMix", 0.75f }, { "spaceMix", 0.08f },
    { "outGain", -6.0f },
};

// Brass reeds in the electrostatic slot -- legal, because brass is a
// conductor and the plate only needs a conductor. A softer, springier tongue
// that swings further for the same blow and rides the slot harder: the bite
// comes out clearly above this preset's own steel twin, and that is the
// measured row. The reed itself also loses energy five times faster (the
// cantilever takes brass's internal loss in full), but measured at the
// OUTPUT the 200A chain's compression hides most of that -- so the decay is
// a voice-level fact here, not a claimed character. The slot is opened and
// the drive eased because the bigger swing otherwise pushes a real DC
// offset through the preamp's asymmetry.
inline constexpr ParamValue kBrassReeds[] = {
    { "instrument", 2.0f }, { "material", 3.0f },
    { "preampDrive", 0.26f }, { "coilSat", 0.35f },
    { "pickupPos", 0.5f }, { "pickupDist", 0.28f },
    { "tremDepth", 0.30f }, { "tremRate", 5.0f },
    { "cabMix", 0.7f }, { "spaceMix", 0.12f },
    { "outGain", -6.0f },
};

// ---------------------------------------------------------------------------
// Grand (acoustic). The mic pair is the instrument's output; presets that
// move the pair do it through the mic bench table below, same contract as
// the tine workshop. Una corda itself is CC67, not a parameter -- the soft
// preset voices the hammers instead.
// ---------------------------------------------------------------------------

// The concert reference: the calibrated mic pair exactly where the grand
// suite measured it, a concert hall's worth of room behind it.
inline constexpr ParamValue kConcertGrand[] = {
    { "instrument", 3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.18f }, { "spaceSize", 0.55f },
};

// The instrument after the technician has been at it. A factory hammer is
// hard and bright, and voicing -- needling the felt so its outer layer
// yields sooner -- is what a concert tuner does before a performance to
// take the edge off the attack without dulling the body. That is a
// softer covering and a slightly gentler blow, not an equaliser: measured
// on the attack's 2-7 kHz band against its own 80-800 Hz, this sits ten
// decibels under the unvoiced instrument, while the sustained centroid
// barely moves.
inline constexpr ParamValue kVoicedGrand[] = {
    { "instrument", 3.0f },
    { "hammerMat", 1.0f }, { "hammerHard", 0.42f },
    { "cabMix", 0.0f }, { "spaceMix", 0.18f }, { "spaceSize", 0.55f },
};

// Close-lid pop: the pair pulled tight, harder hammers, presence up, the
// room nearly shut. The mic bench narrows the pair (table below).
inline constexpr ParamValue kClosePop[] = {
    { "instrument", 3.0f }, { "outGain", -0.5f },
    { "hammerHard", 0.62f }, { "clarity", 2.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.10f }, { "spaceSize", 0.35f },
};

// Past the rim: the pair backed off the rim into the lid's shadow (mic bench
// distance up), the room wide open. Ambient by geometry, not by reverb alone.
inline constexpr ParamValue kPastTheRim[] = {
    { "instrument", 3.0f }, { "clarity", -3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.30f }, { "spaceSize", 0.70f },
};

// The wide pair: spread most of the way out (mic bench), a big slow room --
// the cinematic piano bed.
inline constexpr ParamValue kWideCinema[] = {
    { "instrument", 3.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.35f }, { "spaceSize", 0.85f },
};

// The felt/soft voicing: light curve, the softest useful hammer, the
// mechanism audible the way it is when the strike stops masking it. What
// players reach for where a real una corda would go. Fresh damper felt,
// because the felt-piano sound is a serviced instrument played gently.
inline constexpr ParamValue kFeltGrand[] = {
    { "instrument", 3.0f },
    { "velCurve", 0.62f }, { "hammerHard", 0.22f }, { "hammerMass", 0.62f },
    { "damperFelt", 1.0f },
    { "strikeNoise", 0.30f }, { "clarity", -4.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.25f }, { "spaceSize", 0.60f },
    { "outGain", -2.0f },
};

// The stage grand: dry, dampers gripping a little harder so pedal work stays
// clean under a band -- the sostenuto-and-comping setting.
inline constexpr ParamValue kStageGrand[] = {
    { "instrument", 3.0f },
    { "damperGrip", 0.70f }, { "clarity", 1.5f },
    { "cabMix", 0.0f }, { "spaceMix", 0.08f }, { "spaceSize", 0.30f },
};

// Two presets that ship a mic-stage placement (micStageFor below):
// the club's close pair over the open lid -- the lid image arrives with
// the direct sound and brightens the seat -- and the church's far pair
// with a room to match. The stage tables ride the same factory-table
// mechanism as the pair trims.
inline constexpr ParamValue kJazzClub[] = {
    { "instrument", 3.0f },
    { "clarity", 1.2f },
    { "cabMix", 0.0f }, { "spaceMix", 0.10f }, { "roomProfile", 3.0f },
};
inline constexpr ParamValue kCathedral[] = {
    { "instrument", 3.0f },
    { "damperGrip", 0.85f },
    { "cabMix", 0.0f }, { "spaceMix", 0.30f }, { "roomProfile", 5.0f },
    { "spaceSize", 0.55f },
};

// The parlor instrument: a small maple board. Every board mode moves up and
// the radiation corner with it -- the bottom octave thins the way a short
// piano's bottom octave thins, by geometry, not EQ. The board row measures
// the low band against this preset's own stock-board twin.
inline constexpr ParamValue kParlor[] = {
    { "instrument", 3.0f }, { "outGain", -1.0f },
    { "bodyMat", 2.0f }, { "bodySize", 0.05f },
    { "cabMix", 0.0f }, { "spaceMix", 0.12f }, { "spaceSize", 0.35f },
};

// The board at full size: the whole ladder shifts down, and what that
// audibly buys -- measured, not assumed -- is the released tail: after the
// pedal lifts, the big board's low modes go on carrying where the stock
// board has already let go. The harness row measures exactly that band.
// Departs from Concert Grand at the board and a longer room.
inline constexpr ParamValue kNineFoot[] = {
    { "instrument", 3.0f },
    { "bodySize", 1.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.18f }, { "spaceSize", 0.70f },
};

// Polymer strings on the grand -- legal because the mics hear everything.
// Nylon's bending stiffness is nothing against its tension, so the partials
// sit almost exactly harmonic -- that part is real, and it is what the
// preset is for: no stretch, no beating against the tempered scale, a
// plain harmonic series under the hammer. What it is NOT is bright. The
// bending share carries the material's loss factor, and nylon's is 133x
// the stock wire's, so the top goes an order of magnitude faster and what
// is left is fundamental. A harp with a keyboard: short, clean, dark.
inline constexpr ParamValue kHarpGrand[] = {
    { "instrument", 3.0f }, { "material", 7.0f }, { "outGain", 8.0f },
    { "hammerHard", 0.35f },
    { "cabMix", 0.0f }, { "spaceMix", 0.20f }, { "spaceSize", 0.55f },
};

// The saloon piano: unisons spread a tuner's year apart, worn damper felt,
// hard bright hammers and the mechanism up. The wear is the action bench's,
// not an EQ imitation; the honky-tonk chorus is the detune spread doing what
// it does on a real neglected grand.
inline constexpr ParamValue kSaloon[] = {
    { "instrument", 3.0f },
    { "tipMass", 0.85f }, { "hammerHard", 0.70f },
    { "damperFelt", 2.0f }, { "strikeNoise", 0.32f },
    { "clarity", 1.0f },
    { "cabMix", 0.0f }, { "spaceMix", 0.10f }, { "spaceSize", 0.35f },
};

// ---------------------------------------------------------------------------
// The bank, grouped by instrument: Tine, its workshop, E-Grand, Reed, Grand.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Clav (4). The reference is the D6-style stock voicing: both pickups, the
// Medium rocker, drive at the circuit's own level. Departures say from what.
// ---------------------------------------------------------------------------

// The stock instrument: both bars, Medium, moderate drive, dry.
inline constexpr ParamValue kClavClassic[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 2.0f }, { "clavMed", 1.0f },
    { "preampDrive", 0.30f }, { "spaceMix", 0.06f },
};

// The funk cut: bridge pickup alone, Treble rocker, a harder tangent --
// the percussive end of the instrument.
inline constexpr ParamValue kClavFunk[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 1.0f }, { "clavMed", 0.0f }, { "clavTreb", 1.0f },
    { "hammerHard", 0.65f }, { "preampDrive", 0.38f },
    { "spaceMix", 0.05f },
};

// The instrument that has been out every weekend for thirty years: the
// tangent rubbers notched by use, so the strings catch on release and click
// -- worst in the middle of the compass where the hands live. The wear bench
// at three quarters, and the case a touch louder because a gigged instrument
// is a rattlier one.
inline constexpr ParamValue kGiggedClav[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 2.0f }, { "clavMed", 1.0f },
    { "wearAmount", 0.75f }, { "bodyMix", 0.32f },
    { "preampDrive", 0.33f }, { "spaceMix", 0.06f },
};

// Out of phase: the switch position that keeps only what the two taps do
// not share -- hollow, nasal, unmistakable.
inline constexpr ParamValue kClavPhaseCut[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 3.0f }, { "clavMed", 1.0f },
    { "preampDrive", 0.34f }, { "spaceMix", 0.06f },
};

// Through the classic sweeping filter, the seventies rig.
inline constexpr ParamValue kClavPhaser[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 1.0f }, { "clavTreb", 1.0f }, { "clavMed", 0.0f },
    { "preampDrive", 0.36f },
    { "phaserMix", 0.65f }, { "phaserRate", 0.55f }, { "phaserDepth", 0.75f },
    { "spaceMix", 0.05f },
};

// Soft rocker, center pickup, gentle drive: the mellow end.
inline constexpr ParamValue kClavVelvet[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 0.0f }, { "clavMed", 0.0f }, { "clavSoft", 1.0f },
    { "hammerHard", 0.35f }, { "preampDrive", 0.22f },
    { "spaceMix", 0.10f }, { "spaceSize", 0.45f }, { "outGain", 2.5f },
};

// Brilliant into real drive: the aggressive lead voice.
inline constexpr ParamValue kClavBiting[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 2.0f }, { "clavMed", 0.0f }, { "clavBrill", 1.0f },
    { "preampDrive", 0.55f },
    { "spaceMix", 0.05f },
};

// The aged instrument: the yarn damper compressed to old wool, so the
// release drop hangs out instead of thupping shut, and the clamp losses up
// a touch. The Clav's honest aging lives in the yarn: the felt and rail
// benches reach the hammer instruments only, so this preset does not
// pretend to use them.
inline constexpr ParamValue kAgedClav[] = {
    { "instrument", 4.0f },
    { "clavSwitch", 2.0f }, { "clavMed", 1.0f },
    { "damperGrip", 0.15f }, { "resDamp", 0.45f },
    { "preampDrive", 0.30f },
    { "treble", -1.0f },
    { "spaceMix", 0.08f },
};

inline constexpr Preset kPresets[] = {
    { "Suitcase",      0, kSuitcase,     std::size (kSuitcase) },
    { "Stage DI",      0, kStageDI,      std::size (kStageDI) },
    { "Mellow",        0, kMellow,       std::size (kMellow) },
    { "Ballad",        0, kBallad,       std::size (kBallad) },
    { "Funk",          0, kFunk,         std::size (kFunk) },
    { "Bark",          0, kBark,         std::size (kBark) },
    { "Bell",          0, kBell,         std::size (kBell) },
    { "Amp Tremolo",   0, kAmpTremolo,   std::size (kAmpTremolo) },
    { "Phase 90",      0, kPhase90,      std::size (kPhase90) },
    { "Sylwester",     0, kSylwester,    std::size (kSylwester) },
    { "Electro Tine",  0, kElectroTine,  std::size (kElectroTine) },
    { "Contact Tine",  0, kContactTine,  std::size (kContactTine) },
    { "Bronze Bars",   0, kBronzeBars,   std::size (kBronzeBars) },
    { "Alloy Tines",   0, kAlloyTines,   std::size (kAlloyTines) },
    { "Tired Stage",   0, kTiredStage,   std::size (kTiredStage) },
    { "Just Ballad",   0, kJustBallad,   std::size (kJustBallad) },
    { "Slendro Bells", 0, kSlendroBells, std::size (kSlendroBells) },
    { "Quarter Keys",  0, kQuarterKeys,  std::size (kQuarterKeys) },
    { "Barroom",       0, kBarroom,      std::size (kBarroom) },
    { "Gong Temple",   0, kGongTemple,   std::size (kGongTemple) },

    { "CP-70",         1, kCP70,         std::size (kCP70) },
    { "Bright Pop",    1, kBrightPop,    std::size (kBrightPop) },
    { "Dark Ballad",   1, kDarkBallad,   std::size (kDarkBallad) },
    { "Tack Grand",    1, kTackGrand,    std::size (kTackGrand) },
    { "Magnetic Grand",1, kMagneticGrand,std::size (kMagneticGrand) },
    { "Electro Grand", 1, kElectroGrand, std::size (kElectroGrand) },
    { "Chorus Era",    1, kChorusEra,    std::size (kChorusEra) },
    { "Pedal Wash",    1, kPedalWash,    std::size (kPedalWash) },
    { "Studio Frame",  1, kStudioFrame,  std::size (kStudioFrame) },
    { "Tungsten Wire", 1, kTungstenWire, std::size (kTungstenWire) },
    { "Roadworn",      1, kRoadworn,     std::size (kRoadworn) },

    { "Two Hundred",   2, kTwoHundred,   std::size (kTwoHundred) },
    { "Reed Soul",     2, kReedSoul,     std::size (kReedSoul) },
    { "Sagging Rail",  2, kSaggingRail,  std::size (kSaggingRail) },
    { "Reed Grind",    2, kReedGrind,    std::size (kReedGrind) },
    { "Reed Bark",     2, kReedBark,     std::size (kReedBark) },
    { "Soft Reed",     2, kSoftReed,     std::size (kSoftReed) },
    { "Reed Tremolo",  2, kReedTremolo,  std::size (kReedTremolo) },
    { "Contact Reed",  2, kContactReed,  std::size (kContactReed) },
    { "Lo-Fi Reed",    2, kLoFiReed,     std::size (kLoFiReed) },
    { "Pocket Reed",   2, kPocketReed,   std::size (kPocketReed) },
    { "Brass Reeds",   2, kBrassReeds,   std::size (kBrassReeds) },

    { "Concert Grand", 3, kConcertGrand, std::size (kConcertGrand) },
    { "Voiced Grand",  3, kVoicedGrand,  std::size (kVoicedGrand) },
    { "Close Pop",     3, kClosePop,     std::size (kClosePop) },
    { "Past The Rim",  3, kPastTheRim,   std::size (kPastTheRim) },
    { "Wide Cinema",   3, kWideCinema,   std::size (kWideCinema) },
    { "Felt Grand",    3, kFeltGrand,    std::size (kFeltGrand) },
    { "Stage Grand",   3, kStageGrand,   std::size (kStageGrand) },
    { "Parlor",        3, kParlor,       std::size (kParlor) },
    { "Nine Foot",     3, kNineFoot,     std::size (kNineFoot) },
    { "Harp Grand",    3, kHarpGrand,    std::size (kHarpGrand) },
    { "Jazz Club",     3, kJazzClub,     std::size (kJazzClub) },
    { "Cathedral",     3, kCathedral,    std::size (kCathedral) },
    { "Saloon",        3, kSaloon,       std::size (kSaloon) },

    { "Clav Classic",  4, kClavClassic,  std::size (kClavClassic) },
    { "Clav Funk",     4, kClavFunk,     std::size (kClavFunk) },
    { "Phase Cut",     4, kClavPhaseCut, std::size (kClavPhaseCut) },
    { "Clav Phaser",   4, kClavPhaser,   std::size (kClavPhaser) },
    { "Gigged Clav",   4, kGiggedClav,   std::size (kGiggedClav) },
    { "Velvet Clav",   4, kClavVelvet,   std::size (kClavVelvet) },
    { "Biting Clav",   4, kClavBiting,   std::size (kClavBiting) },
    { "Aged Clav",     4, kAgedClav,     std::size (kAgedClav) },
};

inline constexpr std::size_t kNumPresets = std::size (kPresets);

// ---------------------------------------------------------------------------
// Workshop tables. A preset listed here paints the named bench on load; any
// preset not listed leaves the player's own bench alone.
// ---------------------------------------------------------------------------

// The 200A's own speakers: two 4x8 open-back ovals, breakup at 5.5 kHz,
// short suspension -- the onboard grind (constants in WurliChain.h). Applied
// to every reed preset that plays through the stock cabinet; Contact Reed is
// DI'd and stays off the list.
inline const std::array<float, 5>* cabModsFor (const char* name)
{
    static const std::array<float, 5> wurli { 0.62f, 0.90f, 0.60f, 0.35f, 0.35f };
    for (const char* n : { "Two Hundred", "Reed Soul", "Sagging Rail",
                           "Reed Grind", "Reed Bark", "Soft Reed",
                           "Reed Tremolo", "Lo-Fi Reed", "Pocket Reed",
                           "Brass Reeds" })
        if (std::strcmp (name, n) == 0) return &wurli;
    return nullptr;
}

// The grand's mic bench: { spread, balance, distance, level L, level R }.
// Spread 1 / distance 0 is the calibrated pair the grand suite measured.
inline const std::array<float, 5>* micModsFor (const char* name)
{
    static const std::array<float, 5> close  { 0.80f, 0.0f, 0.00f, 1.0f, 1.0f };
    static const std::array<float, 5> rim    { 1.00f, 0.0f, 1.00f, 1.0f, 1.0f };
    static const std::array<float, 5> cinema { 1.80f, 0.0f, 0.15f, 1.0f, 1.0f };
    if (std::strcmp (name, "Close Pop") == 0)   return &close;
    if (std::strcmp (name, "Past The Rim") == 0) return &rim;
    if (std::strcmp (name, "Wide Cinema") == 0)  return &cinema;
    return nullptr;
}

// Factory mic-stage placements: [mode, five of (on, x, z, h, gainDb, pan)]
// in GrandMicStage units. Only presets named here carry one; every other
// preset leaves the player's stage alone, like the other benches.
inline const std::array<float, 31>* micStageFor (const char* name)
{
    // The club: a close pair over the open lid, the classic seat.
    static const std::array<float, 31> jazz {
        1.0f,
        1.0f, 0.3f, 0.6f, 0.5f, 0.0f, -0.6f,
        1.0f, 0.9f, 0.7f, 0.5f, 0.0f,  0.6f,
        0.0f, 0.0f, 2.5f, 1.0f, 0.0f,  0.0f,
        0.0f, -1.2f, 0.4f, 0.3f, -6.0f, -1.0f,
        0.0f, 1.2f, 0.4f, 0.3f, -6.0f,  1.0f };
    // The church: a far high pair plus a closer center fill.
    static const std::array<float, 31> church {
        1.0f,
        1.0f, -0.8f, 3.2f, 1.4f, 0.0f, -0.8f,
        1.0f,  0.8f, 3.2f, 1.4f, 0.0f,  0.8f,
        1.0f,  0.0f, 1.2f, 0.7f, -4.0f, 0.0f,
        0.0f, -1.2f, 0.4f, 0.3f, -6.0f, -1.0f,
        0.0f,  1.2f, 0.4f, 0.3f, -6.0f,  1.0f };
    if (std::strcmp (name, "Jazz Club") == 0) return &jazz;
    if (std::strcmp (name, "Cathedral") == 0) return &church;
    return nullptr;
}

// The tine workshop tables. Length trims are stored as scale factors; pitch
// follows the beam equation's 1/L^2, so scale = 2^(-cents/2400).
using TineTable = std::array<std::array<float, 2>, 88>;

namespace detail
{
    inline float lenForCents (double cents)
    {
        return static_cast<float> (std::pow (2.0, -cents / 2400.0));
    }

    inline TineTable buildFromCents (const double (&pcCents)[12], double gaugeMid = 1.0)
    {
        TineTable t {};
        for (int i = 0; i < 88; ++i)
        {
            const int note = 21 + i;
            double dia = 1.0;
            if (gaugeMid != 1.0)
            {
                // Fat through the middle of the compass, easing off at both
                // ends: the bass is already massive and the top octave would
                // be overwhelmed by its own hammer.
                const double reg = (note - 21) / 87.0;
                const double bell = std::exp (-std::pow ((reg - 0.45) / 0.30, 2.0));
                dia = 1.0 + (gaugeMid - 1.0) * bell;
            }
            t[static_cast<std::size_t> (i)] = { lenForCents (pcCents[note % 12]),
                                                static_cast<float> (dia) };
        }
        return t;
    }
} // namespace detail

inline const TineTable* tineModsFor (const char* name)
{
    // Five-limit just major on C: thirds at 5/4 and 6/5, the fifth at 3/2.
    static const double kJust[12] = { 0.0, +11.7, +3.9, +15.6, -13.7, -2.0,
                                      -9.8, +2.0, +13.7, -15.6, -3.9, -11.7 };
    // Twelve keys onto five equal slendro degrees of 240 cents: each key
    // snaps to its nearest degree, so neighbouring keys share a pitch.
    static const double kSlendro[12] = { 0.0, -100.0, +40.0, -60.0, +80.0, -20.0,
                                         -120.0, +20.0, -80.0, +60.0, -40.0, +100.0 };
    // The black keys a quarter tone sharp.
    static const double kQuarter[12] = { 0.0, +50.0, 0.0, +50.0, 0.0, 0.0,
                                         +50.0, 0.0, +50.0, 0.0, +50.0, 0.0 };

    static const TineTable just    = detail::buildFromCents (kJust);
    static const TineTable slendro = detail::buildFromCents (kSlendro, 1.45);
    static const TineTable quarter = detail::buildFromCents (kQuarter);
    static const TineTable barroom = [] {
        TineTable t {};
        for (int i = 0; i < 88; ++i)
        {
            // Deterministic per-note scatter, +/- eight cents: the same tine
            // is always the same amount out, which is what a real neglected
            // instrument does and what makes repeated notes sit still.
            const unsigned h = (static_cast<unsigned> (i) * 2654435761u) & 1023u;
            const double cents = (h / 1023.0 - 0.5) * 16.0;
            t[static_cast<std::size_t> (i)] = { detail::lenForCents (cents), 1.0f };
        }
        return t;
    }();
    // Tired Stage drifts, it is not derelict: the same deterministic scatter
    // idea at +/- five cents, hashed differently so the two aged instruments
    // are not the same instrument.
    static const TineTable tired = [] {
        TineTable t {};
        for (int i = 0; i < 88; ++i)
        {
            const unsigned h = ((static_cast<unsigned> (i) + 41u) * 2246822519u) & 1023u;
            const double cents = (h / 1023.0 - 0.5) * 10.0;
            t[static_cast<std::size_t> (i)] = { detail::lenForCents (cents), 1.0f };
        }
        return t;
    }();

    if (std::strcmp (name, "Just Ballad") == 0)   return &just;
    if (std::strcmp (name, "Slendro Bells") == 0) return &slendro;
    // Gong Temple shares the slendro map: the geometry is the point, the
    // bronze and the case are its own departures.
    if (std::strcmp (name, "Gong Temple") == 0)   return &slendro;
    if (std::strcmp (name, "Quarter Keys") == 0)  return &quarter;
    if (std::strcmp (name, "Barroom") == 0)       return &barroom;
    if (std::strcmp (name, "Tired Stage") == 0)   return &tired;
    return nullptr;
}

} // namespace epi::presetdata
