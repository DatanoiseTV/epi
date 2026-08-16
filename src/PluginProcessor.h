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

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/DidgeEngine.h"
#include "common/PresetManager.h"
#include <unordered_map>
#include <vector>

// Top-level plugin. Owns the parameter tree, flattens MIDI into the engine's
// sample-accurate note events, and converts raw parameters into engine units.
class DidgeAudioProcessor : public juce::AudioProcessor
{
public:
    DidgeAudioProcessor();
    ~DidgeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Didge"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    epicommon::PresetManager& getPresetManager() { return presetManager; }

    didge::DidgeEngine&       getEngine()       { return engine; }
    const didge::DidgeEngine& getEngine() const { return engine; }

    bool isHostPlaying() const { return hostPlaying.load (std::memory_order_relaxed); }

    // True when any parameter differs from the last preset load or user save.
    // Computed on demand rather than tracked from a ValueTree callback: the
    // APVTS only pushes parameter changes into its tree on the message thread,
    // so a cached flag goes stale exactly when a host automates a parameter
    // without one.
    bool isCurrentPresetDirty() const { return ! currentMatchesSnapshot(); }

private:
    void snapshotCurrentParams();
    bool currentMatchesSnapshot() const;

    didge::EngineParams buildEngineParams() const;
    void collectEvents (juce::MidiBuffer& midi);

    juce::AudioProcessorValueTreeState apvts;
    didge::DidgeEngine engine;
    epicommon::PresetManager presetManager;

    std::vector<didge::NoteEvent> events;
    std::vector<float> monoScratch;   // mono-host fold-down buffer

    std::atomic<bool> hostPlaying { false };
    std::unordered_map<juce::String, float> cleanSnapshot;

    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DidgeAudioProcessor)
};
