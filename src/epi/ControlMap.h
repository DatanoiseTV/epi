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

#include <cstddef>

// ---------------------------------------------------------------------------
// The external control surface: which continuous controller and which
// registered parameter number reaches each of the instrument's parameters.
//
// This exists so a box of encoders can be an Epi front panel. It is therefore
// a PUBLISHED INTERFACE, not an implementation detail: somebody's hardware,
// somebody's controller template, somebody's sequencer track is keyed to these
// numbers, and a number that moves silently breaks all three. Two consequences
// follow and both are enforced by tests/test_epi_control.cpp:
//
//   * The numbers are written down HERE, explicitly, rather than derived from
//     the parameter layout's order. If they were derived, inserting one
//     parameter would renumber every parameter after it -- an invisible change
//     in a diff that says "added one control".
//   * They are pinned. testControlNumbersArePinned holds a literal copy of
//     every assignment, so changing one fails a test and has to be a decision
//     rather than an accident. New parameters take the next free number.
//
// Two channels, because they answer different needs:
//
//   CC    seven bits, one message, no state. What a knob on a generic
//         controller sends, and what a sequencer lane records. 128 steps is
//         coarse for a physical parameter but it is what the ecosystem speaks.
//
//   NRPN  fourteen bits, four messages, selector state. 16384 steps, which is
//         finer than the interface's own knobs resolve, so a motorised or
//         detented encoder can track a value without stepping audibly. Every
//         parameter has one; only some have a CC.
//
// Both carry the parameter's NORMALISED value -- the same 0..1 the interface
// and the automation lane use -- so a choice parameter and a continuous one
// travel through one code path and round-trip identically.
//
// Where a controller number has a real meaning in the MIDI specification and
// that meaning matches the parameter, the standard number is used: CC 7 is
// Channel Volume and Epi's output level is a channel volume, so it is CC 7.
// Where it does not match, a number from the undefined ranges is used instead.
// Forcing "Vibrato Delay" onto a parameter that is not a vibrato delay would
// make the map look more standard while making it mean less.
//
// Deliberately NOT assigned, because the instrument or the protocol already
// owns them: 0 and 32 (bank select), 1 (modulation -- left for the host to
// map), 6 and 38 (data entry), 7 is used, 11 (expression), 64 (sustain, read
// continuously for half-pedalling), 66 (sostenuto), 67 (soft), 96 and 97 (data
// increment), 98 and 99 (NRPN select), 100 and 101 (RPN select, which the MPE
// tuner reads), and 120-127 (channel mode). CC 14 and 15 are left free on
// purpose as spares.
//
// CC 0-31 have low-order counterparts at CC 32-63. Epi does not read them:
// the fourteen-bit path is NRPN, uniformly, rather than fourteen-bit for the
// twelve parameters that happen to sit below CC 32 and seven-bit for the rest.
// ---------------------------------------------------------------------------
namespace epi
{

struct ControlAssignment
{
    const char* paramId;    // must match an id in src/epi/ParameterIDs.h
    int         cc;         // -1 when the parameter is reachable only by NRPN
    int         nrpn;       // low byte; the high byte (bank) is always 0
    const char* panel;      // where the control lives in the interface
    const char* note;       // why this number, when the reason is not obvious
};

// NRPN numbers are grouped by panel and left with gaps at the end of each
// group, so a parameter added to a panel later can join its neighbours
// instead of being tacked onto the end of the whole map.
inline constexpr ControlAssignment kControlMap[] = {
    // -- Instrument ---------------------------------------------------------
    { "instrument",   90,  0, "Instrument", "which of the five instruments" },
    { "tune",         28,  1, "Instrument", "" },
    { "clarity",       3,  2, "Instrument", "" },

    // -- Action -------------------------------------------------------------
    { "velCurve",     20,  8, "Action", "" },
    { "hammerHard",   73,  9, "Action", "Sound Controller 4, Attack Time" },
    { "hammerMass",   21, 10, "Action", "" },
    { "escapement",   22, 11, "Action", "" },
    { "strikeNoise",  23, 12, "Action", "" },
    { "damperGrip",   75, 13, "Action", "Sound Controller 6, Decay Time" },
    { "keyBed",       24, 14, "Action", "" },
    { "hammerMat",    25, 15, "Action", "" },
    { "damperFelt",   26, 16, "Action", "" },
    { "softMode",     27, 17, "Action", "" },

    // -- Resonator ----------------------------------------------------------
    { "tipMass",      70, 24, "Resonator", "Sound Controller 1, Sound Variation" },
    { "resDamp",      72, 25, "Resonator", "Sound Controller 3, Release Time" },
    { "barCouple",    29, 26, "Resonator", "" },
    { "barTune",      30, 27, "Resonator", "" },
    { "nonlinAmt",    85, 28, "Resonator", "" },
    { "material",     86, 29, "Resonator", "" },
    { "wearAmount",   89, 30, "Resonator", "" },

    // -- Body ---------------------------------------------------------------
    { "bodyMix",      31, 36, "Body", "" },
    { "bodyMat",      87, 37, "Body", "" },
    { "bodySize",     88, 38, "Body", "" },

    // -- Pickup -------------------------------------------------------------
    { "pickupPos",   102, 44, "Pickup", "" },
    { "pickupDist",  103, 45, "Pickup", "" },
    { "pickupSel",   106, 46, "Pickup", "" },
    { "clavSwitch",  107, 47, "Pickup", "" },
    { "coilFreq",    104, 48, "Pickup", "" },
    { "coilQ",       105, 49, "Pickup", "" },
    { "coilSat",      71, 50, "Pickup", "Sound Controller 2, Harmonic Intensity" },

    // -- Amplifier ----------------------------------------------------------
    { "preampDrive", 108, 56, "Amplifier", "" },
    { "bass",        109, 57, "Amplifier", "" },
    { "treble",       74, 58, "Amplifier", "Sound Controller 5, Brightness" },
    { "cabMix",      111, 59, "Amplifier", "" },
    { "clavBrill",   116, 60, "Amplifier", "" },
    { "clavTreb",    117, 61, "Amplifier", "" },
    { "clavMed",     118, 62, "Amplifier", "" },
    { "clavSoft",    119, 63, "Amplifier", "" },

    // -- Modulation ---------------------------------------------------------
    { "tremRate",     76, 70, "Modulation", "Vibrato Rate, the nearest standard rate control" },
    { "tremDepth",    92, 71, "Modulation", "Effects 2 Depth, defined as Tremolo Depth" },
    { "tremStereo",  110, 72, "Modulation", "" },
    { "phaserMix",    95, 73, "Modulation", "Effects 5 Depth, defined as Phaser Depth" },
    { "phaserRate",  112, 74, "Modulation", "" },
    { "phaserDepth", 113, 75, "Modulation", "" },
    { "phaserFb",    114, 76, "Modulation", "" },

    // -- Output -------------------------------------------------------------
    { "spaceMix",     91, 82, "Output", "Effects 1 Depth, defined as Reverb Send" },
    { "spaceSize",   115, 83, "Output", "" },
    { "roomProfile",   9, 84, "Output", "" },
    { "outGain",       7, 85, "Output", "Channel Volume" },
};

inline constexpr int kNumControls = (int) (sizeof (kControlMap) / sizeof (kControlMap[0]));

// The NRPN bank. Every parameter lives in bank 0; the byte is reserved so a
// later surface (per-note benches, say) can be added without disturbing this
// one.
inline constexpr int kNrpnBank = 0;

inline constexpr int controlIndexForCc (int cc)
{
    for (int i = 0; i < kNumControls; ++i)
        if (kControlMap[i].cc == cc) return i;
    return -1;
}

inline constexpr int controlIndexForNrpn (int bank, int number)
{
    if (bank != kNrpnBank) return -1;
    for (int i = 0; i < kNumControls; ++i)
        if (kControlMap[i].nrpn == number) return i;
    return -1;
}

} // namespace epi
