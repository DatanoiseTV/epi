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

#include <juce_core/juce_core.h>

// ---------------------------------------------------------------------------
// Which parameters the editor binds, and how.
//
// This list decides what the interface can reach, and the state suite checks
// it against the parameter layout and against the UI bundle's own tables --
// which is how a knob that reached a panel without a formatter gets caught
// before it throws in a plugin window rather than after.
//
// It lives in its own header, away from the editor, for a build reason worth
// stating: the editor drags in JUCE's browser module, and that module needs a
// platform SDK that only a plugin target arranges -- GTK headers on Linux,
// the WebView2 package on Windows. A console test that merely wants to know
// which parameters are bound should not have to satisfy either, and when it
// did, the whole Linux build failed over a suite about state persistence and
// then the whole Windows one did.
//
// The two kinds are declared separately because they fail differently in the
// UI: a continuous parameter needs an entry in the bundle's PARAMS table
// (the knob reads its formatter from there), while a choice parameter takes
// its value from the combo relay and needs an entry in the browser mock's
// list instead.
// ---------------------------------------------------------------------------
namespace epi::ui
{
    inline constexpr const char* kFloatIds[] = {
        "tune",
        "velCurve", "hammerHard", "hammerMass", "escapement", "strikeNoise", "damperGrip",
        "tipMass", "resDamp", "barCouple", "barTune", "bodyMix", "nonlinAmt",
        "pickupPos", "pickupDist", "coilFreq", "coilQ", "coilSat",
        "preampDrive", "bass", "treble", "tremRate", "tremDepth", "tremStereo", "cabMix",
        "phaserMix", "phaserRate", "phaserDepth", "phaserFb",
        "spaceMix", "spaceSize", "outGain", "clarity",
        "clavBrill", "clavTreb", "clavMed", "clavSoft",
        "bodySize", "wearAmount",
    };

    // No bool parameters exist; an empty constexpr array is a compiler
    // extension GCC and MSVC both reject, so there is simply no list.
    inline constexpr const char* kChoiceIds[] = {
        "pickupSel", "instrument", "material", "clavSwitch", "bodyMat",
        "damperFelt", "keyBed", "hammerMat", "roomProfile", "softMode",
    };

    inline juce::StringArray boundParameterIds()
    {
        juce::StringArray ids;
        for (auto id : kFloatIds)  ids.add (id);
        for (auto id : kChoiceIds) ids.add (id);
        return ids;
    }
} // namespace epi::ui
