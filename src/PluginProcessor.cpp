/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "presets/FactoryPresets.h"
#include "ui/WebEditor.h"

#ifndef DIDGE_VERSION
 #define DIDGE_VERSION "dev"   // set by CMake on the plugin target
#endif

namespace
{
    // Carried in the state tree beside the parameters.
    const juce::Identifier kPresetNameProperty { "presetName" };
}

DidgeAudioProcessor::DidgeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", didge::ids::createParameterLayout()),
      presetManager (apvts,
                     { "Didge", "DidgePreset", DIDGE_VERSION },
                     didge::makeFactoryPresets())
{
    events.reserve (256);
    presetManager.setPostLoadHook ([this] { snapshotCurrentParams(); });
    snapshotCurrentParams();
}

DidgeAudioProcessor::~DidgeAudioProcessor() = default;

bool DidgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DidgeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    engine.prepare (newSampleRate, juce::jmax (16, samplesPerBlock));
}

didge::EngineParams DidgeAudioProcessor::buildEngineParams() const
{
    using namespace didge::ids;
    auto raw = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed);
    };

    didge::EngineParams p;
    p.pressure    = raw (pressure);
    p.attackMs    = raw (attack);
    p.releaseMs   = raw (release);
    p.vibRate     = raw (vibRate);
    p.vibDepth    = raw (vibDepth);
    p.breath      = raw (breathNoise);
    p.decayOn     = raw (decayOn) > 0.5f;
    p.decayMs     = raw (decay);
    p.sustain     = raw (sustain);

    p.velTarget   = static_cast<int> (raw (velTarget));
    p.velAmount   = raw (velAmount);
    p.humanize    = raw (humanize);

    p.tensionSemis = raw (tension);
    p.lipDamp      = raw (lipDamp);
    p.embouchure   = raw (embouchure);

    p.tractMix    = raw (tractMix);
    p.vowelX      = raw (vowelX);
    p.vowelY      = raw (vowelY);
    p.growl       = raw (growl);
    p.growlSemis  = raw (growlPitch);

    p.tuneCents      = raw (tune);
    p.shape.bell     = raw (bell);
    p.shape.flare    = raw (flare);
    p.shape.texture  = raw (texture);
    p.shape.wallDamp = raw (wallDamp);
    p.shape.diameter = raw (boreDia);
    p.shape.profile  = static_cast<int> (raw (boreProfile));
    p.shape.material = static_cast<int> (raw (material));
    p.exciter        = static_cast<int> (raw (exciter));

    p.spaceMix   = raw (spaceMix);
    p.spaceSize  = raw (spaceSize);
    p.outGainLin = juce::Decibels::decibelsToGain (raw (outGain));
    return p;
}

void DidgeAudioProcessor::collectEvents (juce::MidiBuffer& midi)
{
    events.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int  t = meta.samplePosition;

        if (m.isNoteOn())
            events.push_back ({ t, didge::NoteEvent::noteOn, m.getNoteNumber(),
                                m.getFloatVelocity() });
        else if (m.isNoteOff())
            events.push_back ({ t, didge::NoteEvent::noteOff, m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ t, didge::NoteEvent::allNotesOff, 0, 0.0f });
        else if (m.isPitchWheel())
        {
            const float range = apvts.getRawParameterValue (didge::ids::bendRange)
                                     ->load (std::memory_order_relaxed);
            engine.setPitchBend ((m.getPitchWheelValue() - 8192) / 8192.0f * range);
        }
        else if (m.isController())
        {
            // CC2 (breath) and CC11 (expression) scale the blowing pressure —
            // the natural mapping for a wind instrument, and what a breath
            // controller sends.
            const int cc = m.getControllerNumber();
            if (cc == 2 || cc == 11)
                engine.setPressureScale (m.getControllerValue() / 127.0f * 1.5f);
        }
    }
}

void DidgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || numOut < 1)
        return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            hostPlaying.store (pos->getIsPlaying(), std::memory_order_relaxed);

    collectEvents (midi);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : left;

    if (numOut > 1)
    {
        engine.process (left, right, n, buildEngineParams(), events.data(), (int) events.size());
    }
    else
    {
        // Mono host: render into a scratch right channel and fold.
        std::vector<float>& scratch = monoScratch;
        if ((int) scratch.size() < n) scratch.resize ((size_t) n);
        engine.process (left, scratch.data(), n, buildEngineParams(),
                        events.data(), (int) events.size());
        for (int i = 0; i < n; ++i)
            left[i] = 0.5f * (left[i] + scratch[(size_t) i]);
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

juce::AudioProcessorEditor* DidgeAudioProcessor::createEditor()
{
    return new didge::WebEditor (*this);
}

void DidgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // The preset name lives outside the parameter tree, so it has to be put
    // in by hand. Without this a session reloads with every value the user
    // left behind but the name of whatever preset was loaded first, shown as
    // clean -- which reads as though nothing was saved at all.
    state.setProperty (kPresetNameProperty, presetManager.getCurrentName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DidgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            snapshotCurrentParams();

            if (state.hasProperty (kPresetNameProperty))
                presetManager.setCurrentName (state[kPresetNameProperty].toString());
        }
}

void DidgeAudioProcessor::snapshotCurrentParams()
{
    cleanSnapshot.clear();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            cleanSnapshot[rp->getParameterID()] = rp->getValue();
}

bool DidgeAudioProcessor::currentMatchesSnapshot() const
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto it = cleanSnapshot.find (rp->getParameterID());
            if (it == cleanSnapshot.end() || std::abs (it->second - rp->getValue()) > 1.0e-4f)
                return false;
        }
    return true;
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DidgeAudioProcessor();
}
