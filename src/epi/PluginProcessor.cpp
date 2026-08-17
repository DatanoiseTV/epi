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

#include "epi/PluginProcessor.h"
#include "epi/ParameterIDs.h"
#include "epi/presets/FactoryPresets.h"
#include "epi/ui/WebEditor.h"

#ifndef EPI_VERSION
 #define EPI_VERSION "dev"
#endif

namespace
{
    // Carried in the state tree beside the parameters.
    const juce::Identifier kPresetNameProperty { "presetName" };
}

EpiAudioProcessor::EpiAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", epi::ids::createParameterLayout()),
      presetManager (apvts,
                     { "Epi", "EpiPreset", EPI_VERSION },
                     epi::makeFactoryPresets())
{
    events.reserve (256);
    presetManager.setPostLoadHook ([this]
    {
        snapshotCurrentParams();
        // A workshop preset re-cuts the harp; any other preset leaves the
        // player's own workshop alone.
        if (const auto* mods = epi::factoryTineMods (presetManager.getCurrentName()))
            for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
                setTineMod (i, (*mods)[static_cast<std::size_t> (i)][0],
                               (*mods)[static_cast<std::size_t> (i)][1]);
        if (const auto* cab = epi::factoryCabMods (presetManager.getCurrentName()))
            setCabMod ({ (*cab)[0], (*cab)[1], (*cab)[2], (*cab)[3], (*cab)[4] });
    });
    snapshotCurrentParams();
}

EpiAudioProcessor::~EpiAudioProcessor() = default;

bool EpiAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void EpiAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    engine.prepare (newSampleRate, samplesPerBlock);
    monoScratch.assign (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);
}

epi::EngineParams EpiAudioProcessor::buildEngineParams() const
{
    auto raw = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed);
    };

    epi::EngineParams p;
    using namespace epi::ids;

    p.instrument = static_cast<int> (raw (instrument));

    p.tuneCents  = raw (tune);

    p.velCurve    = raw (velCurve);
    p.hammerHard  = raw (hammerHard);
    p.hammerMass  = raw (hammerMass);
    p.escapement  = raw (escapement);
    p.strikeNoise = raw (strikeNoise);
    p.damperGrip  = raw (damperGrip);

    p.tipMass   = raw (tipMass);
    p.resDamp   = raw (resDamp);
    p.barCouple = raw (barCouple);
    p.barTune   = raw (barTune);
    p.bodyMix   = raw (bodyMix);
    p.nonlinAmt = raw (nonlinAmt);

    p.pickupPos  = raw (pickupPos);
    p.pickupDist = raw (pickupDist);
    p.pickupSel  = static_cast<int> (raw (pickupSel));
    p.coilFreq   = raw (coilFreq);
    p.coilQ      = raw (coilQ);
    p.coilSat    = raw (coilSat);

    p.preampDrive = raw (preampDrive);
    p.bassDb      = raw (bass);
    p.trebleDb    = raw (treble);
    p.tremRate    = raw (tremRate);
    p.tremDepth   = raw (tremDepth);
    p.tremStereo  = raw (tremStereo);
    p.cabMix      = raw (cabMix);
    p.phaserMix   = raw (phaserMix);
    p.phaserRate  = raw (phaserRate);
    p.phaserDepth = raw (phaserDepth);
    p.phaserFb    = raw (phaserFb);

    p.spaceMix   = raw (spaceMix);
    p.spaceSize  = raw (spaceSize);
    p.outGainLin = juce::Decibels::decibelsToGain (raw (outGain));

    return p;
}

void EpiAudioProcessor::collectEvents (juce::MidiBuffer& midi)
{
    events.clear();

    // Notes clicked on the interface, queued from the message thread.
    for (auto r = uiNoteRead.load (std::memory_order_relaxed);
         r != uiNoteWrite.load (std::memory_order_acquire);
         uiNoteRead.store (++r, std::memory_order_release))
    {
        const auto& u = uiNotes[r % kUiNoteCap];
        if (u.note >= 0 && u.note < 128)
            events.push_back ({ 0, u.on ? epi::NoteEvent::noteOn : epi::NoteEvent::noteOff,
                                u.note, u.on ? std::max (0.05f, u.velocity) : 0.0f });
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int t = meta.samplePosition;

        if (m.isNoteOn())
            events.push_back ({ t, epi::NoteEvent::noteOn, m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff())
            events.push_back ({ t, epi::NoteEvent::noteOff, m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ t, epi::NoteEvent::allNotesOff, 0, 0.0f });
        else if (m.isSustainPedalOn())
            events.push_back ({ t, epi::NoteEvent::sustainOn, 0, 0.0f });
        else if (m.isSustainPedalOff())
            events.push_back ({ t, epi::NoteEvent::sustainOff, 0, 0.0f });
        else if (m.isPitchWheel())
        {
            // Bend is applied at block rate; it does not need a sample-accurate
            // event because nothing about it is transient.
            const float norm = (m.getPitchWheelValue() - 8192) / 8192.0f;
            engine.setPitchBend (norm * 2.0f);
        }
        else if (m.isController() && m.getControllerNumber() == 11)
        {
            engine.setExpression (0.25f + 0.75f * m.getControllerValue() / 127.0f);
        }
    }
}

void EpiAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numOut = buffer.getNumChannels();
    buffer.clear();
    if (n == 0 || numOut < 1) return;

    collectEvents (midi);
    const auto params = buildEngineParams();

    float* left = buffer.getWritePointer (0);

    if (numOut >= 2)
    {
        engine.process (left, buffer.getWritePointer (1), n, params,
                        events.data(), static_cast<int> (events.size()));
        for (int ch = 2; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    }
    else
    {
        if (static_cast<int> (monoScratch.size()) < n) monoScratch.assign (static_cast<size_t> (n), 0.0f);
        engine.process (left, monoScratch.data(), n, params,
                        events.data(), static_cast<int> (events.size()));
        for (int i = 0; i < n; ++i) left[i] = 0.5f * (left[i] + monoScratch[static_cast<size_t> (i)]);
    }
}

juce::AudioProcessorEditor* EpiAudioProcessor::createEditor()
{
    return new epi::WebEditor (*this);
}

void EpiAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kPresetNameProperty, presetManager.getCurrentName(), nullptr);

    // The workshop's per-tine steel travels with the project, not with
    // presets: a preset is a sound, the workshop is a modification to the
    // instrument itself. Stored as two comma-joined lists, and only when
    // anything differs from stock, so untouched sessions stay byte-identical.
    bool modded = false;
    for (const auto& m : tineMods)
        if (m[0] != 1.0f || m[1] != 1.0f) { modded = true; break; }
    for (const auto& m : pickupMods)
        if (m[0] != 0.0f || m[1] != 0.0f || m[2] != 1.0f) { modded = true; break; }
    for (const auto& m : stringMods)
        if (m[0] != 1.0f || m[1] != 1.0f) { modded = true; break; }
    if (cabMods != kCabDefaults) modded = true;
    if (modded)
    {
        juce::StringArray ls, ds, hs, gs, ws;
        for (const auto& m : tineMods)
        {
            ls.add (juce::String (m[0], 6));
            ds.add (juce::String (m[1], 6));
        }
        for (const auto& m : pickupMods)
        {
            hs.add (juce::String (m[0], 6));
            gs.add (juce::String (m[1], 6));
            ws.add (juce::String (m[2], 6));
        }
        auto mods = juce::ValueTree ("TineMods");
        mods.setProperty ("len", ls.joinIntoString (","), nullptr);
        mods.setProperty ("dia", ds.joinIntoString (","), nullptr);
        mods.setProperty ("pkh", hs.joinIntoString (","), nullptr);
        mods.setProperty ("pkg", gs.joinIntoString (","), nullptr);
        mods.setProperty ("pkw", ws.joinIntoString (","), nullptr);
        juce::StringArray sl, sd;
        for (const auto& m : stringMods)
        {
            sl.add (juce::String (m[0], 6));
            sd.add (juce::String (m[1], 6));
        }
        mods.setProperty ("slen", sl.joinIntoString (","), nullptr);
        mods.setProperty ("sdia", sd.joinIntoString (","), nullptr);
        juce::StringArray cs;
        for (float v : cabMods) cs.add (juce::String (v, 6));
        mods.setProperty ("cab", cs.joinIntoString (","), nullptr);
        state.appendChild (mods, nullptr);
    }

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void EpiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            snapshotCurrentParams();
            if (state.hasProperty (kPresetNameProperty))
                presetManager.setCurrentName (state[kPresetNameProperty].toString());

            // Restore the workshop -- or return the harp to stock when the
            // incoming state has no modifications.
            resetTineMods();
            resetPickupMods();
            resetStringMods();
            resetCabMods();
            const auto mods = state.getChildWithName ("TineMods");
            if (mods.isValid())
            {
                juce::StringArray ls, ds, hs, gs, ws;
                ls.addTokens (mods["len"].toString(), ",", "");
                ds.addTokens (mods["dia"].toString(), ",", "");
                hs.addTokens (mods["pkh"].toString(), ",", "");
                gs.addTokens (mods["pkg"].toString(), ",", "");
                ws.addTokens (mods["pkw"].toString(), ",", "");
                for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
                {
                    setTineMod (i,
                                i < ls.size() ? ls[i].getFloatValue() : 1.0f,
                                i < ds.size() ? ds[i].getFloatValue() : 1.0f);
                    setPickupMod (i,
                                  i < hs.size() ? hs[i].getFloatValue() : 0.0f,
                                  i < gs.size() ? gs[i].getFloatValue() : 0.0f,
                                  i < ws.size() ? ws[i].getFloatValue() : 1.0f);
                }
                juce::StringArray sl, sd;
                sl.addTokens (mods["slen"].toString(), ",", "");
                sd.addTokens (mods["sdia"].toString(), ",", "");
                for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
                    setStringMod (i,
                                  i < sl.size() ? sl[i].getFloatValue() : 1.0f,
                                  i < sd.size() ? sd[i].getFloatValue() : 1.0f);
                juce::StringArray cs;
                cs.addTokens (mods["cab"].toString(), ",", "");
                if (cs.size() == 5)
                    setCabMod ({ cs[0].getFloatValue(), cs[1].getFloatValue(),
                                 cs[2].getFloatValue(), cs[3].getFloatValue(),
                                 cs[4].getFloatValue() });
            }
        }
}

void EpiAudioProcessor::snapshotCurrentParams()
{
    cleanSnapshot.clear();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            cleanSnapshot[rp->getParameterID()] = rp->getValue();
}

bool EpiAudioProcessor::currentMatchesSnapshot() const
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto it = cleanSnapshot.find (rp->getParameterID());
            if (it == cleanSnapshot.end()) return false;
            if (std::abs (it->second - rp->getValue()) > 1.0e-4f) return false;
        }
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EpiAudioProcessor();
}
