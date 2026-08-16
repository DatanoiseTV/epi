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

#include "FactoryPresets.h"
#include "epi/ParameterIDs.h"

namespace epi
{

std::vector<epicommon::PresetManager::Preset> makeFactoryPresets()
{
    using namespace epi::ids;
    return {
        // The instrument as a well-maintained one leaves the shop: voiced a
        // little off the pole centreline, which is where the fundamental lives.
        { "Suitcase", {
            { pickupPos, -0.35f }, { pickupDist, 0.35f },
            { coilFreq, 0.50f }, { coilQ, 0.50f }, { coilSat, 0.25f },
            { hammerHard, 0.50f }, { resDamp, 0.32f }, { barCouple, 0.60f },
            { preampDrive, 0.30f }, { bass, 2.0f }, { treble, 1.0f },
            { tremRate, 4.2f }, { tremDepth, 0.35f }, { cabMix, 0.55f },
        } },

        // Voiced hard against the centreline. The tine crosses the field's peak
        // twice a cycle, so the note comes out an octave up with almost no
        // fundamental. A real setting, and the reason a badly voiced Rhodes
        // sounds thin rather than quiet.
        { "Bell", {
            { pickupPos, -0.04f }, { pickupDist, 0.22f },
            { hammerHard, 0.68f }, { resDamp, 0.18f }, { barCouple, 0.85f },
            { coilFreq, 0.70f }, { coilQ, 0.65f },
            { preampDrive, 0.18f }, { treble, 3.5f },
            { tremDepth, 0.0f }, { cabMix, 0.40f },
        } },

        // Toward the edge of the pole, where the swing is most asymmetric.
        { "Mellow", {
            { pickupPos, -0.80f }, { pickupDist, 0.55f },
            { hammerHard, 0.28f }, { resDamp, 0.40f }, { barCouple, 0.45f },
            { coilFreq, 0.30f }, { coilQ, 0.35f },
            { preampDrive, 0.15f }, { bass, 4.0f }, { treble, -2.0f },
            { tremRate, 3.2f }, { tremDepth, 0.25f }, { cabMix, 0.65f },
        } },

        // Close to the magnet and played into it: the tine runs off the ends of
        // the pole face, where the field collapses. That is the growl.
        { "Dirty Bass", {
            { pickupPos, -0.45f }, { pickupDist, 0.08f },
            { hammerHard, 0.72f }, { hammerMass, 0.70f },
            { resDamp, 0.25f }, { nonlinAmt, 0.85f },
            { coilFreq, 0.42f }, { coilQ, 0.55f }, { coilSat, 0.60f },
            { preampDrive, 0.72f }, { bass, 5.0f },
            { cabMix, 0.80f },
        } },

        // Long sustain, gentle touch, the pedal-down sound.
        { "Ballad", {
            { pickupPos, -0.55f }, { pickupDist, 0.40f },
            { hammerHard, 0.32f }, { velCurve, 0.34f },
            { resDamp, 0.12f }, { damperGrip, 0.42f }, { barCouple, 0.70f },
            { coilFreq, 0.44f }, { coilQ, 0.42f },
            { preampDrive, 0.12f }, { bass, 3.0f },
            { tremRate, 2.6f }, { tremDepth, 0.30f }, { cabMix, 0.55f },
            { spaceMix, 0.30f }, { spaceSize, 0.55f },
        } },

        // Hard tip, tight damper, no vibrato: the percussive setting.
        { "Funk", {
            { pickupPos, -0.30f }, { pickupDist, 0.25f },
            { hammerHard, 0.82f }, { hammerMass, 0.35f }, { velCurve, 0.68f },
            { resDamp, 0.55f }, { damperGrip, 0.85f }, { barCouple, 0.35f },
            { coilFreq, 0.62f }, { coilQ, 0.70f },
            { preampDrive, 0.45f }, { treble, 4.0f },
            { tremDepth, 0.0f }, { cabMix, 0.50f },
        } },

        // A pole piece narrower and closer than Rhodes ever built, and a coil
        // that peaks high. Not a filter setting -- a different magnet.
        { "Glass Tine", {
            { pickupPos, -0.18f }, { pickupDist, 0.06f },
            { hammerHard, 0.55f }, { tipMass, 0.80f },
            { resDamp, 0.20f }, { barCouple, 0.90f }, { barTune, 7.0f },
            { coilFreq, 0.88f }, { coilQ, 0.85f }, { coilSat, 0.10f },
            { preampDrive, 0.22f }, { treble, 5.0f },
            { tremRate, 6.5f }, { tremDepth, 0.20f }, { cabMix, 0.30f },
            { spaceMix, 0.25f },
        } },

        // The tuning spring slid a long way down the tine, which does not just
        // retune it -- it sits at a different fraction of every mode's shape and
        // re-voices the overtones with it.
        { "Detuned Bar", {
            { pickupPos, -0.50f }, { pickupDist, 0.30f },
            { tipMass, 0.08f }, { barTune, -9.0f }, { barCouple, 0.95f },
            { resDamp, 0.22f }, { nonlinAmt, 0.70f },
            { coilFreq, 0.38f }, { coilQ, 0.48f },
            { preampDrive, 0.30f }, { bass, 2.0f },
            { tremRate, 5.5f }, { tremDepth, 0.45f }, { cabMix, 0.60f },
        } },
    };
}

} // namespace epi
