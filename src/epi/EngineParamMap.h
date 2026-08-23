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

#include "epi/dsp/EpiEngine.h"

#include <cmath>

// The one mapping from raw parameter values to engine units. The processor's
// buildEngineParams delegates here, and the preset verification harness builds
// its EngineParams through the same function -- so what the tests render is,
// by construction, what the plugin renders. Framework-free on purpose: the
// harness compiles without JUCE.
//
// The ids are the strings from epi/ParameterIDs.h. They are repeated here as
// literals because that header needs JUCE and this one must not; the preset
// harness asserts that every id requested below exists in its replica of the
// parameter layout, so a typo in either place fails the suite rather than
// silently reading a dead parameter.
namespace epi
{

// `raw` returns a parameter's current value in real (un-normalised) units,
// looked up by id -- for the processor that is the APVTS raw value, for the
// tests a table.
template <typename RawFn>
inline EngineParams engineParamsFrom (const RawFn& raw)
{
    EngineParams p;

    p.instrument = static_cast<int> (raw ("instrument"));

    p.tuneCents  = raw ("tune");

    p.velCurve    = raw ("velCurve");
    p.hammerHard  = raw ("hammerHard");
    p.hammerMass  = raw ("hammerMass");
    p.escapement  = raw ("escapement");
    p.strikeNoise = raw ("strikeNoise");
    p.damperGrip  = raw ("damperGrip");

    p.tipMass   = raw ("tipMass");
    p.resDamp   = raw ("resDamp");
    p.barCouple = raw ("barCouple");
    p.barTune   = raw ("barTune");
    p.bodyMix   = raw ("bodyMix");
    p.nonlinAmt = raw ("nonlinAmt");

    p.pickupPos  = raw ("pickupPos");
    p.pickupDist = raw ("pickupDist");
    p.pickupSel  = static_cast<int> (raw ("pickupSel"));
    p.coilFreq   = raw ("coilFreq");
    p.coilQ      = raw ("coilQ");
    p.coilSat    = raw ("coilSat");

    p.preampDrive = raw ("preampDrive");
    p.bassDb      = raw ("bass");
    p.trebleDb    = raw ("treble");
    p.clarityDb   = raw ("clarity");
    p.transducer  = static_cast<int> (raw ("pickupSel"));
    p.material    = static_cast<int> (raw ("material"));
    p.clavSwitch  = static_cast<int> (raw ("clavSwitch"));
    p.bodyMat     = static_cast<int> (raw ("bodyMat"));
    p.bodySize    = raw ("bodySize");
    p.damperFelt  = static_cast<int> (raw ("damperFelt"));
    p.keyBed      = static_cast<int> (raw ("keyBed"));
    p.hammerMat   = static_cast<int> (raw ("hammerMat"));
    p.roomProfile = static_cast<int> (raw ("roomProfile"));
    p.clavBrill   = raw ("clavBrill") > 0.5f;
    p.clavTreb    = raw ("clavTreb") > 0.5f;
    p.clavMed     = raw ("clavMed") > 0.5f;
    p.clavSoft    = raw ("clavSoft") > 0.5f;
    p.tremRate    = raw ("tremRate");
    p.tremDepth   = raw ("tremDepth");
    p.tremStereo  = raw ("tremStereo");
    p.cabMix      = raw ("cabMix");
    p.phaserMix   = raw ("phaserMix");
    p.phaserRate  = raw ("phaserRate");
    p.phaserDepth = raw ("phaserDepth");
    p.phaserFb    = raw ("phaserFb");

    p.spaceMix  = raw ("spaceMix");
    p.spaceSize = raw ("spaceSize");
    // dB to linear, the same arithmetic juce::Decibels::decibelsToGain does.
    // The parameter's floor is -24 dB, nowhere near the -100 dB silence
    // convention, so the plain power law is the whole behaviour.
    p.outGainLin = std::pow (10.0f, raw ("outGain") * 0.05f);

    return p;
}

} // namespace epi
