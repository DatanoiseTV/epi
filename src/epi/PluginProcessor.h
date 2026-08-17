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

#include "common/PresetManager.h"
#include "epi/dsp/EpiEngine.h"

#include <unordered_map>
#include <vector>

// Top-level plugin. Owns the parameter tree, flattens MIDI into the engine's
// sample-accurate note events, and converts raw parameters into engine units.
class EpiAudioProcessor : public juce::AudioProcessor
{
public:
    EpiAudioProcessor();
    ~EpiAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Epi"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // A Rhodes with the pedal down rings a very long time.
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    epicommon::PresetManager& getPresetManager() { return presetManager; }

    epi::EpiEngine&       getEngine()       { return engine; }
    const epi::EpiEngine& getEngine() const { return engine; }

    // A note played from the interface (clicking the drawn keys). Called on
    // the message thread; drained into the engine's event list at the top of
    // the next block. A fixed ring with atomic indices -- one producer, one
    // consumer -- so the audio thread never takes a lock for it.
    // The workshop's per-tine steel: message-thread copy for state save and
    // for the interface to read back; every write is forwarded to the engine.
    void setTineMod (int index, float lenScale, float diaScale)
    {
        if (index < 0 || index >= epi::EpiEngine::kNumTines) return;
        tineMods[static_cast<std::size_t> (index)] = { lenScale, diaScale };
        engine.setTineMod (index, lenScale, diaScale);
    }
    void resetTineMods()
    {
        for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
            setTineMod (i, 1.0f, 1.0f);
    }
    const std::array<std::array<float, 2>, epi::EpiEngine::kNumTines>& getTineMods() const
    {
        return tineMods;
    }

    void pushUiNote (int note, float velocity, bool on)
    {
        const auto w = uiNoteWrite.load (std::memory_order_relaxed);
        const auto r = uiNoteRead.load (std::memory_order_acquire);
        if (w - r >= kUiNoteCap) return;                    // full: drop, UI notes are advisory
        uiNotes[w % kUiNoteCap] = { note, velocity, on };
        uiNoteWrite.store (w + 1, std::memory_order_release);
    }

    // True when any parameter differs from the last preset load or user save.
    // Computed on demand rather than tracked from a ValueTree callback: the
    // APVTS only pushes parameter changes into its tree on the message thread,
    // so a cached flag goes stale exactly when a host automates a parameter
    // without one.
    bool isCurrentPresetDirty() const { return ! currentMatchesSnapshot(); }

private:
    void snapshotCurrentParams();
    bool currentMatchesSnapshot() const;

    epi::EngineParams buildEngineParams() const;
    void collectEvents (juce::MidiBuffer& midi);

    juce::AudioProcessorValueTreeState apvts;

    std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> tineMods = [] {
        std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> a {};
        for (auto& m : a) m = { 1.0f, 1.0f };
        return a;
    }();

    struct UiNote { int note; float velocity; bool on; };
    static constexpr unsigned kUiNoteCap = 64;
    std::array<UiNote, kUiNoteCap> uiNotes {};
    std::atomic<unsigned> uiNoteWrite { 0 }, uiNoteRead { 0 };
    epi::EpiEngine engine;
    epicommon::PresetManager presetManager;

    std::vector<epi::NoteEvent> events;
    std::vector<float> monoScratch;

    std::unordered_map<juce::String, float> cleanSnapshot;

    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EpiAudioProcessor)
};
