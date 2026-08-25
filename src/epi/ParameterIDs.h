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
    // Only what exists. The other three are modelled in docs/research and not
    // in code, and a selector that offers them is a control that does nothing --
    // which is precisely the defect this project has just spent a day removing
    // from six other knobs. They go back on the list when they play.
    // Two real instruments now, so the selector returns. It was removed when
    // only one existed: a single-entry choice has the range [0, 0] and AU
    // validation reads back NaN from the normalisation divide.
    inline const juce::StringArray instrumentNames { "Tine", "E-Grand", "Reed", "Grand", "Clav" };

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
    inline constexpr const char* pickupSel  = "pickupSel";  // choice: transducer swap
    inline constexpr const char* coilFreq   = "coilFreq";   // 0..1 resonance
    inline constexpr const char* coilQ      = "coilQ";      // 0..1
    inline constexpr const char* coilSat    = "coilSat";    // 0..1 core saturation

    inline const juce::StringArray hammerMatNames {
        "Stock", "Soft Felt", "Hard Felt", "Lacquered", "Leather", "Wood" };
    inline const juce::StringArray damperFeltNames {
        "Stock", "Fresh", "Worn", "Hardened" };
    inline const juce::StringArray keyBedNames {
        "Stock", "Fresh Felt", "Leather", "Worn" };

    // The output-stage room. Custom is the shipped size-mapped room,
    // bit-exact; the rest are surveyed spaces with Eyring decays derived
    // from published absorption data (Room.h, room-acoustics-measured.md).
    inline const juce::StringArray roomProfileNames {
        "Custom", "Booth", "Studio", "Stage", "Hall", "Church" };

    // The left pedal's two real mechanisms (grand path): Shift slides the
    // action sideways -- two strings, off-centre felt; Rail is the upright's
    // half-blow -- the hammer stroke shortens and the action goes loose.
    inline const juce::StringArray softModeNames { "Shift", "Rail" };

    inline const juce::StringArray bodyMatNames {
        // Index 0 is whatever the instrument was calibrated with.
        "Stock", "Spruce", "Maple", "Birch Ply",
        "Aluminium", "Steel", "Brass", "Carbon" };

    // The Clavinet's pickup switch. Order must match epi::PickupSelect
    // (neck, bridge, bothIn, bothOut) -- EngineParamMap passes the index
    // through as clavSwitch.
    inline const juce::StringArray clavSwitchNames {
        // The D6 selector rockers resolved to their four states.
        "Center", "Bridge", "Both", "Out of Phase" };

    inline const juce::StringArray materialNames {
        // Index 0 is the calibrated stock metal for every instrument.
        "Music Wire", "Stainless", "Bronze", "Brass",
        "Titanium", "Aluminium", "Tungsten", "Nylon" };

    inline const juce::StringArray pickupSelNames {
        // Order is state: index 1 has been the stored default since the
        // parameter existed, so NATIVE lives there and old sessions keep
        // their sound. MAGNETIC is a coil reading flux, ELECTRO a polarised
        // capacitor, CONTACT the force at the mount.
        "Magnetic", "Native", "Electro", "Contact" };

    // ---- Amplifier ----------------------------------------------------------
    inline constexpr const char* preampDrive = "preampDrive"; // 0..1
    inline constexpr const char* bass        = "bass";        // dB
    inline constexpr const char* treble      = "treble";      // dB
    inline constexpr const char* tremRate    = "tremRate";    // Hz
    inline constexpr const char* tremDepth   = "tremDepth";   // 0..1
    inline constexpr const char* clarity     = "clarity";     // -12..+12 dB air
    inline constexpr const char* material    = "material";    // choice: resonator metal
    inline constexpr const char* clavSwitch  = "clavSwitch";  // choice: the D6 pickup matrix
    inline constexpr const char* clavBrill   = "clavBrill";   // rocker: Brilliant
    inline constexpr const char* clavTreb    = "clavTreb";    // rocker: Treble
    inline constexpr const char* clavMed     = "clavMed";     // rocker: Medium
    inline constexpr const char* clavSoft    = "clavSoft";    // rocker: Soft
    inline constexpr const char* bodyMat     = "bodyMat";     // choice: frame/board material
    inline constexpr const char* bodySize    = "bodySize";    // 0..1, 0.5 = stock scale
    inline constexpr const char* damperFelt  = "damperFelt";  // choice: felt condition
    inline constexpr const char* keyBed      = "keyBed";      // choice: rail cloth
    inline constexpr const char* hammerMat   = "hammerMat";   // choice: hammer covering
    inline constexpr const char* roomProfile = "roomProfile"; // choice: which room
    inline constexpr const char* softMode    = "softMode";    // choice: CC67 mechanism
    inline constexpr const char* wearAmount  = "wearAmount";  // 0..1 tangent-rubber wear
    inline constexpr const char* tremStereo  = "tremStereo";  // 0..1
    inline constexpr const char* cabMix      = "cabMix";      // 0..1
    inline constexpr const char* phaserMix   = "phaserMix";   // 0..1
    inline constexpr const char* phaserRate  = "phaserRate";  // Hz
    inline constexpr const char* phaserDepth = "phaserDepth"; // 0..1
    inline constexpr const char* phaserFb    = "phaserFb";    // 0..1

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
        // No selector while there is only one instrument to select. Beyond
        // being a control that does nothing, a choice parameter with a single
        // entry has the range [0, 0], and normalising a value across it divides
        // by zero -- Audio Unit validation reads back NaN and fails the plugin
        // outright. The id and the name list are kept for when the others play.
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
        unit (strikeNoise, "Key Noise",   0.22f);
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
                                  Rng { -1.0f, 1.0f, 0.0f }, -0.35f,
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
                                      // Must track staticGap in RhodesVoice::configure.
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
        // At 1 the two photocells run in opposition and the note is panned
        // across the speaker pair, which is what a Rhodes calls vibrato. At 0
        // they run together and it is a true amplitude tremolo.
        add (std::make_unique<P> (juce::ParameterID { tremStereo, 1 }, "Trem Width",
                                  Rng { 0.0f, 1.0f, 0.0f }, 1.0f,
                                  attrs ([] (float v, int)
                                  {
                                      return v > 0.99f ? juce::String ("Pan")
                                           : v < 0.01f ? juce::String ("Amplitude")
                                           : juce::String (juce::roundToInt (v * 100.0f)) + "% pan";
                                  })));
        unit (cabMix,    "Cabinet", 0.5f);

        // ---- Phaser ---------------------------------------------------------
        unit (phaserMix,   "Phaser",       0.0f);
        add (std::make_unique<P> (juce::ParameterID { phaserRate, 1 }, "Phaser Rate",
                                  Rng { 0.02f, 8.0f, 0.0f, 0.35f }, 0.40f,
                                  attrs ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
        unit (phaserDepth, "Phaser Depth", 0.70f);
        unit (phaserFb,    "Phaser Res",   0.50f);

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

        // ---- Instrument, appended at the end per the rule above -------------
        add (std::make_unique<Pc> (juce::ParameterID { instrument, 1 }, "Instrument",
                                   instrumentNames, 0));

        // Appended at the end for the same reason the instrument selector
        // was: hosts index AU parameters by position.
        // The third tone control: an air shelf above the treble's reach,
        // where "clarity" actually lives.
        add (std::make_unique<P> (juce::ParameterID { clarity, 1 }, "Clarity",
                                   juce::NormalisableRange<float> { -12.0f, 12.0f, 0.0f }, 0.0f));

        // What the tine, string, or reed is made of. Index 0 is the stock
        // metal and the exact calibrated instrument; appended at the end
        // because hosts index AU parameters by position.
        add (std::make_unique<Pc> (juce::ParameterID { material, 1 }, "Material",
                                   materialNames, 0));

        // The Clavinet's own switches, appended at the end like everything
        // after the first release. The pickup matrix is the D6's two-rocker
        // selector; the four tone rockers are stepped floats because they
        // are honest on/off switches with no bool infrastructure to lean on.
        add (std::make_unique<Pc> (juce::ParameterID { clavSwitch, 1 }, "Clav Pickup",
                                   clavSwitchNames, 0));
        add (std::make_unique<P> (juce::ParameterID { clavBrill, 1 }, "Brilliant",
                                   juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f }, 0.0f));
        add (std::make_unique<P> (juce::ParameterID { clavTreb, 1 }, "Treble Rocker",
                                   juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f }, 0.0f));
        add (std::make_unique<P> (juce::ParameterID { clavMed, 1 }, "Medium Rocker",
                                   juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f }, 1.0f));
        add (std::make_unique<P> (juce::ParameterID { clavSoft, 1 }, "Soft Rocker",
                                   juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f }, 0.0f));

        // The body bench: what the frame, bar or board is made of and how
        // big it is. Appended at the end, stock-by-default, bit-exact there.
        add (std::make_unique<Pc> (juce::ParameterID { bodyMat, 1 }, "Body Material",
                                   bodyMatNames, 0));
        add (std::make_unique<P> (juce::ParameterID { bodySize, 1 }, "Body Size",
                                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0f }, 0.5f));

        // The action bench: the damper felt's condition (hardened felt lets
        // the high partials escape -- the zing) and the rail cloth the keys
        // land on. Stock is the calibrated instrument.
        add (std::make_unique<Pc> (juce::ParameterID { damperFelt, 1 }, "Damper Felt",
                                   damperFeltNames, 0));
        add (std::make_unique<Pc> (juce::ParameterID { keyBed, 1 }, "Key Bed",
                                   keyBedNames, 0));
        add (std::make_unique<Pc> (juce::ParameterID { hammerMat, 1 }, "Hammer Covering",
                                   hammerMatNames, 0));

        // The room the output stage plays into.
        add (std::make_unique<Pc> (juce::ParameterID { roomProfile, 1 }, "Room",
                                   roomProfileNames, 0));

        // Which mechanism CC67 drives on the grand.
        add (std::make_unique<Pc> (juce::ParameterID { softMode, 1 }, "Soft Pedal",
                                   softModeNames, 0));

        // Tangent-rubber wear on the Clav: 0 is the papers' mint instrument,
        // 1 a gigged-hard one -- notched rubbers that catch and click,
        // unevenly per key (practitioner report, 2026).
        unit (wearAmount, "Wear", 0.0f);

        return { params.begin(), params.end() };
    }
} // namespace epi::ids
