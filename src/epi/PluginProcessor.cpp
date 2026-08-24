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
#include "epi/EngineParamMap.h"
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
    presetManager.setExtraState (juce::Identifier { "TineMods" },
        [this] { return buildModsTree (true); },
        [this] (const juce::ValueTree& t) { applyModsTree (t); });
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
        if (const auto* mic = epi::factoryMicMods (presetManager.getCurrentName()))
            setMicMod ({ (*mic)[0], (*mic)[1], (*mic)[2], (*mic)[3], (*mic)[4] });
        if (const auto* st = epi::factoryMicStage (presetManager.getCurrentName()))
            setMicStage (*st);
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
    // The raw-to-engine mapping lives in EngineParamMap.h, shared verbatim
    // with the preset verification harness: what the tests render is what
    // this processor renders.
    return epi::engineParamsFrom ([this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed);
    });
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
        else if (m.isController() && m.getControllerNumber() == 66)
            events.push_back ({ t, epi::NoteEvent::sostenuto, 0,
                                m.getControllerValue() >= 64 ? 1.0f : 0.0f });
        else if (m.isController() && m.getControllerNumber() == 67)
            events.push_back ({ t, epi::NoteEvent::soft, 0,
                                m.getControllerValue() >= 64 ? 1.0f : 0.0f });
        else if (m.isController() && m.getControllerNumber() == 64)
        {
            // The full CC64 value, not the on/off half: the damper felt is a
            // contact damping term, so pedal depth scales the decay rate and
            // half-pedalling works the way a real damper rail does.
            events.push_back ({ t, epi::NoteEvent::sustain, 0,
                                static_cast<float> (m.getControllerValue()) / 127.0f });
        }
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

    // The workshop benches travel with the project. Stored only when
    // anything differs from stock, so untouched sessions stay byte-identical.
    const auto mods = buildModsTree (false);
    if (mods.isValid()) state.appendChild (mods, nullptr);

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

            // Restore the benches -- or return everything to stock when the
            // incoming state carries no modifications.
            applyModsTree (state.getChildWithName ("TineMods"));
        }
}

// The workshop benches as one subtree: the tine and string tables, the
// pickup errors, the cabinet. `always` false returns an invalid tree when
// everything is stock, so the project state can stay byte-identical for
// untouched sessions; the preset path passes true, because a saved preset
// is a complete snapshot by contract.
juce::ValueTree EpiAudioProcessor::buildModsTree (bool always) const
{
    bool modded = false;
    for (const auto& m : tineMods)
        if (m[0] != 1.0f || m[1] != 1.0f) { modded = true; break; }
    for (const auto& m : pickupMods)
        if (m[0] != 0.0f || m[1] != 0.0f || m[2] != 1.0f) { modded = true; break; }
    for (const auto& m : stringMods)
        if (m[0] != 1.0f || m[1] != 1.0f) { modded = true; break; }
    for (const auto& m : grandMods)
        if (m[0] != 1.0f || m[1] != 1.0f) { modded = true; break; }
    if (cabMods != kCabDefaults) modded = true;
    if (micMods != kMicDefaults) modded = true;
    if (micStage != kStageDefaults) modded = true;
    if (velMap != std::array<float, 5> { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) modded = true;
    if (! modded && ! always) return {};

    juce::StringArray ls, ds, hs, gs, ws, sl, sd, cs;
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
    for (const auto& m : stringMods)
    {
        sl.add (juce::String (m[0], 6));
        sd.add (juce::String (m[1], 6));
    }
    juce::StringArray gl, gd;
    for (const auto& m : grandMods)
    {
        gl.add (juce::String (m[0], 6));
        gd.add (juce::String (m[1], 6));
    }
    for (float v : cabMods) cs.add (juce::String (v, 6));
    juce::StringArray ms;
    for (float v : micMods) ms.add (juce::String (v, 6));

    juce::ValueTree mods ("TineMods");
    mods.setProperty ("len",  ls.joinIntoString (","), nullptr);
    mods.setProperty ("dia",  ds.joinIntoString (","), nullptr);
    mods.setProperty ("pkh",  hs.joinIntoString (","), nullptr);
    mods.setProperty ("pkg",  gs.joinIntoString (","), nullptr);
    mods.setProperty ("pkw",  ws.joinIntoString (","), nullptr);
    mods.setProperty ("slen", sl.joinIntoString (","), nullptr);
    mods.setProperty ("sdia", sd.joinIntoString (","), nullptr);
    mods.setProperty ("glen", gl.joinIntoString (","), nullptr);
    mods.setProperty ("gdia", gd.joinIntoString (","), nullptr);
    mods.setProperty ("cab",  cs.joinIntoString (","), nullptr);
    mods.setProperty ("mic",  ms.joinIntoString (","), nullptr);
    juce::StringArray sts;
    for (float v : micStage) sts.add (juce::String (v, 6));
    mods.setProperty ("mstage", sts.joinIntoString (","), nullptr);
    juce::StringArray vm;
    for (float v : velMap) vm.add (juce::String (v, 6));
    mods.setProperty ("vmap", vm.joinIntoString (","), nullptr);
    return mods;
}

// Reset every bench to stock, then apply whatever the tree carries. An
// invalid tree therefore means "stock instrument", and a tree missing one
// list leaves that bench at stock -- both are what an older save means.
void EpiAudioProcessor::applyModsTree (const juce::ValueTree& mods)
{
    resetTineMods();
    resetPickupMods();
    resetStringMods();
    resetGrandMods();
    resetCabMods();
    resetMicMods();
    resetMicStage();
    resetVelMap();
    if (! mods.isValid()) return;

    juce::StringArray ls, ds, hs, gs, ws, sl, sd, cs;
    ls.addTokens (mods["len"].toString(), ",", "");
    ds.addTokens (mods["dia"].toString(), ",", "");
    hs.addTokens (mods["pkh"].toString(), ",", "");
    gs.addTokens (mods["pkg"].toString(), ",", "");
    ws.addTokens (mods["pkw"].toString(), ",", "");
    sl.addTokens (mods["slen"].toString(), ",", "");
    sd.addTokens (mods["sdia"].toString(), ",", "");
    juce::StringArray gl, gd;
    gl.addTokens (mods["glen"].toString(), ",", "");
    gd.addTokens (mods["gdia"].toString(), ",", "");
    cs.addTokens (mods["cab"].toString(), ",", "");
    juce::StringArray msv;
    msv.addTokens (mods["mic"].toString(), ",", "");
    juce::StringArray stv;
    stv.addTokens (mods["mstage"].toString(), ",", "");
    if (stv.size() == 31)
    {
        std::array<float, 31> sv {};
        for (int i = 0; i < 31; ++i) sv[static_cast<size_t> (i)] = stv[i].getFloatValue();
        setMicStage (sv);
    }
    juce::StringArray vmv;
    vmv.addTokens (mods["vmap"].toString(), ",", "");
    for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
    {
        setTineMod (i,
                    i < ls.size() ? ls[i].getFloatValue() : 1.0f,
                    i < ds.size() ? ds[i].getFloatValue() : 1.0f);
        setPickupMod (i,
                      i < hs.size() ? hs[i].getFloatValue() : 0.0f,
                      i < gs.size() ? gs[i].getFloatValue() : 0.0f,
                      i < ws.size() ? ws[i].getFloatValue() : 1.0f);
        setStringMod (i,
                      i < sl.size() ? sl[i].getFloatValue() : 1.0f,
                      i < sd.size() ? sd[i].getFloatValue() : 1.0f);
        setGrandMod (i,
                     i < gl.size() ? gl[i].getFloatValue() : 1.0f,
                     i < gd.size() ? gd[i].getFloatValue() : 1.0f);
    }
    if (cs.size() == 5)
        setCabMod ({ cs[0].getFloatValue(), cs[1].getFloatValue(),
                     cs[2].getFloatValue(), cs[3].getFloatValue(),
                     cs[4].getFloatValue() });
    if (msv.size() == 5)
        setMicMod ({ msv[0].getFloatValue(), msv[1].getFloatValue(),
                     msv[2].getFloatValue(), msv[3].getFloatValue(),
                     msv[4].getFloatValue() });
    if (vmv.size() == 5)
        setVelMap ({ vmv[0].getFloatValue(), vmv[1].getFloatValue(),
                     vmv[2].getFloatValue(), vmv[3].getFloatValue(),
                     vmv[4].getFloatValue() });
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
