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

#include <juce_audio_processors/juce_audio_processors.h>

// Single source of truth for parameter IDs. The WebEditor re-quotes these on
// the JS side (PARAMS ids in ui/epi/juce-bridge.jsx); keep both in sync — on
// the JS side a typo becomes a dead control, not a compile error, which is why
// testEditorBindsEveryParameter exists on the C++ side.
//
// The controls are named after the physical thing they change, not after the
// effect they have. "Pickup Height" moves the tine relative to the magnet, the
// way the voicing screw does on the real instrument; it is not a tone control
// that happens to sound like one.
namespace epi::ids
{
    // ---- Instrument ---------------------------------------------------------
    inline constexpr const char* instrument = "instrument";  // choice
    inline constexpr const char* tune       = "tune";        // cents

    // Order must match epi::Instrument in dsp/EpiModel.h.
    inline const juce::StringArray instrumentNames {
        "Rhodes", "Wurlitzer", "Clavinet", "CP-70"
    };

    // ---- Action: key, hammer, damper ---------------------------------------
    inline constexpr const char* velCurve    = "velCurve";    // 0..1
    inline constexpr const char* hammerHard  = "hammerHard";  // 0..1
    inline constexpr const char* hammerMass  = "hammerMass";  // 0..1
    inline constexpr const char* escapement  = "escapement";  // 0..1
    inline constexpr const char* strikeNoise = "strikeNoise"; // 0..1
    inline constexpr const char* damperGrip  = "damperGrip";  // 0..1

    // ---- Resonator: the vibrating metal and what it is bolted to -----------
    inline constexpr const char* tipMass   = "tipMass";   // 0..1 tuning spring / solder
    inline constexpr const char* resDamp   = "resDamp";   // 0..1 -> sustain length
    inline constexpr const char* barCouple = "barCouple"; // 0..1 tonebar coupling
    inline constexpr const char* barTune   = "barTune";   // semitones
    inline constexpr const char* bodyMix   = "bodyMix";   // 0..1 frame/case resonance
    inline constexpr const char* nonlinAmt = "nonlinAmt"; // 0..1 large-deflection

    // ---- Transducer ---------------------------------------------------------
    // The single most important control on a Rhodes: where the tine sits in
    // the magnet's field. Aligned with the centreline the field is symmetric
    // and the output is rich in upper partials; toward the edge the
    // fundamental dominates. On the real instrument this is a screw.
    inline constexpr const char* pickupPos  = "pickupPos";  // -1..1
    inline constexpr const char* pickupDist = "pickupDist"; // 0..1 gap
    inline constexpr const char* pickupSel  = "pickupSel";  // choice (Clavinet)
    inline constexpr const char* coilFreq   = "coilFreq";   // 0..1 resonance
    inline constexpr const char* coilQ      = "coilQ";      // 0..1
    inline constexpr const char* coilSat    = "coilSat";    // 0..1 core saturation

    // Clavinet pickup switching. Order must match epi::PickupSelect.
    inline const juce::StringArray pickupSelNames {
        "Neck", "Bridge", "Both +", "Both −"
    };

    // ---- Amplifier ----------------------------------------------------------
    inline constexpr const char* preampDrive = "preampDrive"; // 0..1
    inline constexpr const char* bass        = "bass";        // dB
    inline constexpr const char* treble      = "treble";      // dB
    inline constexpr const char* tremRate    = "tremRate";    // Hz
    inline constexpr const char* tremDepth   = "tremDepth";   // 0..1
    inline constexpr const char* cabMix      = "cabMix";      // 0..1

    // ---- Output -------------------------------------------------------------
    inline constexpr const char* spaceMix  = "spaceMix";  // 0..1
    inline constexpr const char* spaceSize = "spaceSize"; // 0..1
    inline constexpr const char* outGain   = "outGain";   // dB

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using P   = juce::AudioParameterFloat;
        using Pc  = juce::AudioParameterChoice;
        using Rng = juce::NormalisableRange<float>;

        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        auto add = [&params] (auto p) { params.push_back (std::move (p)); };

        auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        auto attrs = [] (auto fn)
        {
            return juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (fn);
        };
        auto dB = [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };

        auto unit = [&] (const char* id, const char* name, float def)
        {
            add (std::make_unique<P> (juce::ParameterID { id, 1 }, name,
                                      Rng { 0.0f, 1.0f, 0.0f }, def, attrs (pct)));
        };

        // ---- Instrument -----------------------------------------------------
        add (std::make_unique<Pc> (juce::ParameterID { instrument, 1 }, "Instrument",
                                   instrumentNames, 0));
        add (std::make_unique<P> (juce::ParameterID { tune, 1 }, "Tune",
                                  Rng { -100.0f, 100.0f, 0.1f }, 0.0f,
                                  attrs ([] (float v, int) { return juce::String (v, 1) + " ct"; })));

        // ---- Action ---------------------------------------------------------
        // 0.5 is the linear response. Below it the instrument gives more for a
        // light touch, above it the player has to work for the top of the range.
        unit (velCurve,    "Velocity Curve", 0.5f);
        unit (hammerHard,  "Hammer",         0.5f);
        unit (hammerMass,  "Hammer Mass",    0.5f);
        unit (escapement,  "Escapement",     0.4f);
        unit (strikeNoise, "Action Noise",   0.3f);
        unit (damperGrip,  "Damper",         0.6f);

        // ---- Resonator ------------------------------------------------------
        unit (tipMass,   "Tuning Spring", 0.5f);
        unit (resDamp,   "Damping",       0.35f);
        unit (barCouple, "Tone Bar",      0.6f);
        add (std::make_unique<P> (juce::ParameterID { barTune, 1 }, "Bar Tune",
                                  Rng { -24.0f, 24.0f, 0.1f }, 0.0f,
                                  attrs ([] (float v, int)
                                  { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; })));
        unit (bodyMix,   "Body",          0.25f);
        unit (nonlinAmt, "Bloom",         0.5f);

        // ---- Transducer -----------------------------------------------------
        add (std::make_unique<P> (juce::ParameterID { pickupPos, 1 }, "Pickup Height",
                                  Rng { -1.0f, 1.0f, 0.0f }, 0.0f,
                                  attrs ([] (float v, int)
                                  {
                                      // Shown as the real offset in millimetres:
                                      // the adjustment range on the instrument is
                                      // about +/- 2 mm about the centreline.
                                      return juce::String (v * 2.0f, 2) + " mm";
                                  })));
        add (std::make_unique<P> (juce::ParameterID { pickupDist, 1 }, "Pickup Gap",
                                  Rng { 0.0f, 1.0f, 0.0f }, 0.35f,
                                  attrs ([] (float v, int)
                                  {
                                      return juce::String (0.6f + 4.4f * v, 2) + " mm";
                                  })));
        add (std::make_unique<Pc> (juce::ParameterID { pickupSel, 1 }, "Pickup",
                                   pickupSelNames, 1));
        unit (coilFreq, "Coil Peak", 0.5f);
        unit (coilQ,    "Coil Q",    0.5f);
        unit (coilSat,  "Core Sat",  0.25f);

        // ---- Amplifier ------------------------------------------------------
        unit (preampDrive, "Drive", 0.3f);
        add (std::make_unique<P> (juce::ParameterID { bass, 1 }, "Bass",
                                  Rng { -12.0f, 12.0f, 0.1f }, 0.0f, attrs (dB)));
        add (std::make_unique<P> (juce::ParameterID { treble, 1 }, "Treble",
                                  Rng { -12.0f, 12.0f, 0.1f }, 0.0f, attrs (dB)));
        {
            Rng r { 0.1f, 12.0f };
            r.setSkewForCentre (4.0f);
            add (std::make_unique<P> (juce::ParameterID { tremRate, 1 }, "Tremolo Rate", r, 5.5f,
                                      attrs ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
        }
        unit (tremDepth, "Tremolo", 0.0f);
        unit (cabMix,    "Cabinet", 0.5f);

        // ---- Output ---------------------------------------------------------
        unit (spaceMix,  "Space",      0.15f);
        unit (spaceSize, "Space Size", 0.40f);
        add (std::make_unique<P> (juce::ParameterID { outGain, 1 }, "Output",
                                  Rng { -24.0f, 12.0f, 0.1f }, 0.0f, attrs (dB)));

        // ---------------------------------------------------------------------
        // New parameters go at the END of this layout, never in the middle of
        // it. Hosts address parameters by their position as well as their id,
        // so inserting one beside the controls it belongs with shifts every
        // parameter after it, and a session saved by an earlier build reloads
        // with each value landing on the wrong control. Grouping is the user
        // interface's job — panels.jsx puts these where they belong.
        // testParameterOrderIsStable pins the published order.
        // ---------------------------------------------------------------------

        return { params.begin(), params.end() };
    }
} // namespace epi::ids
