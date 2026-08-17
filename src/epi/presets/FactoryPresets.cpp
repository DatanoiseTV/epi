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

// The factory bank, re-engineered from the measurements rather than inherited
// from guesses -- every earlier bank predated the pole geometry, the
// sensitivity, the sustain law, the saturation and the effects, so its stored
// numbers no longer meant what they meant when they were chosen.
//
// Ground rules, learned the hard way and enforced here:
//
//  - "Suitcase" IS the reference: the voicing the suite verifies against a
//    real 1977 Mark I, every row passing. Other presets are deliberate
//    departures and say from what.
//  - The pickup gap stays at or above about 1.4 mm (knob 0.20). Below that
//    the tine leaves the pole face twice a cycle, and measurement showed what
//    that does: the inharmonic floor up 45 dB, the fundamental beating
//    against itself, the attack collapsed to a click. Dirt comes from the
//    DRIVE and the core saturation, which is where the instrument gets it.
//  - Every parameter the engine has is set explicitly, so a preset is a
//    complete voicing and not a diff against whatever was loaded before.

std::vector<epicommon::PresetManager::Preset> makeFactoryPresets()
{
    using namespace epi::ids;

    // The parameters shared by every preset unless it says otherwise: the
    // reference instrument, effects off.
    auto base = [] (std::initializer_list<std::pair<const char*, float>> diff)
    {
        std::vector<std::pair<juce::String, float>> p = {
            { tune, 0.0f },
            { velCurve, 0.5f }, { hammerHard, 0.5f }, { hammerMass, 0.5f },
            { escapement, 0.4f }, { strikeNoise, 0.22f }, { damperGrip, 0.6f },
            { tipMass, 0.5f }, { resDamp, 0.35f },
            { barCouple, 0.6f }, { barTune, 0.0f },
            { bodyMix, 0.25f }, { nonlinAmt, 0.5f },
            { pickupPos, -0.35f }, { pickupDist, 0.35f },
            { coilFreq, 0.5f }, { coilQ, 0.5f }, { coilSat, 0.25f },
            { preampDrive, 0.30f }, { bass, 0.0f }, { treble, 0.0f },
            { tremRate, 5.5f }, { tremDepth, 0.0f }, { tremStereo, 1.0f },
            { cabMix, 0.5f },
            { phaserMix, 0.0f }, { phaserRate, 0.40f },
            { phaserDepth, 0.70f }, { phaserFb, 0.50f },
            { spaceMix, 0.12f }, { spaceSize, 0.40f },
            { outGain, 0.0f },
        };
        for (auto& d : diff)
            for (auto& e : p)
                if (e.first == juce::String (d.first)) { e.second = d.second; break; }
        return p;
    };

    return {
        // The instrument the suite verifies: a well-maintained Mark I into its
        // own Suitcase amplifier, panner running gently. Change nothing here
        // without re-running the suite -- this preset is its baseline.
        { "Suitcase", base ({
            { tremDepth, 0.35f }, { tremRate, 4.2f },
            { bass, 2.0f }, { treble, 1.0f }, { cabMix, 0.55f },
        }) },

        // The same harp DI'd through a bright amp: no panner, less cabinet,
        // a little more edge. What most records of a Stage actually are.
        { "Stage DI", base ({
            { cabMix, 0.30f }, { treble, 2.5f },
            { preampDrive, 0.35f }, { spaceMix, 0.08f },
        }) },

        // Voiced dark: pickup low and far, soft tip, felt loose. The gap
        // stays legal -- the softness comes from where the softness comes
        // from on the instrument, the voicing screw and the hammer.
        { "Mellow", base ({
            { pickupPos, -0.70f }, { pickupDist, 0.45f },
            { hammerHard, 0.28f }, { resDamp, 0.42f },
            { coilFreq, 0.32f }, { coilQ, 0.38f },
            { preampDrive, 0.18f }, { bass, 3.0f }, { treble, -2.0f },
            { tremDepth, 0.25f }, { tremRate, 3.2f }, { cabMix, 0.6f },
        }) },

        // The ballad setting: long sustain (bar fully coupled -- that is what
        // the coupling control now honestly does), soft touch, wide slow pan,
        // a real room.
        { "Ballad", base ({
            { barCouple, 0.9f }, { hammerHard, 0.32f }, { velCurve, 0.38f },
            { resDamp, 0.18f }, { pickupPos, -0.5f },
            { preampDrive, 0.15f }, { bass, 2.0f },
            { tremDepth, 0.30f }, { tremRate, 2.6f },
            { spaceMix, 0.30f }, { spaceSize, 0.60f },
        }) },

        // Percussive: hard tip, tight damper, bar coupling backed off so the
        // notes get out of each other's way, no modulation.
        { "Funk", base ({
            { hammerHard, 0.80f }, { velCurve, 0.65f },
            { damperGrip, 0.85f }, { barCouple, 0.42f }, { resDamp, 0.5f },
            { coilFreq, 0.62f }, { coilQ, 0.66f },
            { preampDrive, 0.45f }, { treble, 3.5f },
            { cabMix, 0.45f }, { spaceMix, 0.06f },
        }) },

        // Driven hard, the honest way: the gap is LEGAL, and the dirt is the
        // preamp run into its knee plus the cores into saturation -- which is
        // what an overdriven Rhodes is.
        { "Bark", base ({
            { pickupDist, 0.25f }, { pickupPos, -0.30f },
            { hammerHard, 0.70f }, { hammerMass, 0.62f },
            { coilSat, 0.55f }, { preampDrive, 0.78f },
            { bass, 3.0f }, { treble, 1.5f }, { cabMix, 0.7f },
            { spaceMix, 0.05f },
        }) },

        // Voiced on the pole centreline: the tine crosses the field peak
        // twice a cycle and the note comes out an octave up with almost no
        // fundamental. A real setting -- the reason a badly voiced Rhodes
        // sounds thin -- kept because it teaches what the voicing screw does.
        { "Bell", base ({
            { pickupPos, -0.05f }, { pickupDist, 0.32f },
            { hammerHard, 0.6f }, { barCouple, 0.8f },
            { coilFreq, 0.7f }, { coilQ, 0.6f },
            { preampDrive, 0.2f }, { treble, 3.0f },
            { cabMix, 0.35f }, { spaceMix, 0.18f },
        }) },

        // The Wurlitzer's kind of tremolo on the Rhodes' tone: photocells
        // wired TOGETHER, so it pulses instead of panning.
        { "Amp Tremolo", base ({
            { tremDepth, 0.55f }, { tremRate, 5.8f }, { tremStereo, 0.0f },
            { preampDrive, 0.35f }, { cabMix, 0.6f },
        }) },

        // A Rhodes through a slow phaser, which is one of the two or three
        // sounds the instrument is known for.
        { "Phase 90", base ({
            { phaserMix, 0.55f }, { phaserRate, 0.35f },
            { phaserDepth, 0.80f }, { phaserFb, 0.62f },
            { tremDepth, 0.0f }, { cabMix, 0.5f }, { spaceMix, 0.15f },
        }) },
    };
}

} // namespace epi
