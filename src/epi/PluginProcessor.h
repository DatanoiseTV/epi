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

    // The string workshop: the CP-70's per-course steel.
    void setVelMap (const std::array<float, 5>& y)
    {
        velMap = y;
        engine.setVelMap (y.data());
    }
    void resetVelMap() { setVelMap ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }); }
    const std::array<float, 5>& getVelMap() const { return velMap; }

    void setGrandMod (int index, float lenScale, float diaScale)
    {
        if (index < 0 || index >= epi::EpiEngine::kNumTines) return;
        grandMods[static_cast<std::size_t> (index)] = { lenScale, diaScale };
        engine.setGrandMod (index, lenScale, diaScale);
    }
    void resetGrandMods()
    {
        for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
            setGrandMod (i, 1.0f, 1.0f);
    }
    const std::array<std::array<float, 2>, epi::EpiEngine::kNumTines>& getGrandMods() const
    {
        return grandMods;
    }

    void setStringMod (int index, float lenScale, float diaScale)
    {
        if (index < 0 || index >= epi::EpiEngine::kNumTines) return;
        stringMods[static_cast<std::size_t> (index)] = { lenScale, diaScale };
        engine.setStringMod (index, lenScale, diaScale);
    }
    void resetStringMods()
    {
        for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
            setStringMod (i, 1.0f, 1.0f);
    }
    const std::array<std::array<float, 2>, epi::EpiEngine::kNumTines>& getStringMods() const
    {
        return stringMods;
    }

    // The pickup workshop's per-pickup errors: height offset, gap offset,
    // winding scale.
    void setPickupMod (int index, float heightOff, float gapOff, float sens)
    {
        if (index < 0 || index >= epi::EpiEngine::kNumTines) return;
        pickupMods[static_cast<std::size_t> (index)] = { heightOff, gapOff, sens };
        engine.setPickupMod (index, heightOff, gapOff, sens);
    }
    void resetPickupMods()
    {
        for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
            setPickupMod (i, 0.0f, 0.0f, 1.0f);
    }
    const std::array<std::array<float, 3>, epi::EpiEngine::kNumTines>& getPickupMods() const
    {
        return pickupMods;
    }

    // The cabinet workshop's five dimensions.
    void setCabMod (const std::array<float, 5>& v)
    {
        cabMods = v;
        engine.setCabMod (v[0], v[1], v[2], v[3], v[4]);
    }
    void setMicMod (const std::array<float, 5>& v)
    {
        micMods = v;
        engine.setMicMod (v[0], v[1], v[2], v[3], v[4]);
    }
    void resetMicMods() { setMicMod (kMicDefaults); }
    const std::array<float, 5>& getMicMods() const { return micMods; }

    // The mic stage: [mode, then five of (on, x, z, h, gainDb, pan)] in
    // GrandMicStage's own units. Mode 0 ignores the mic list (classic pair),
    // so the defaults may carry a sensible starting placement.
    void setMicStage (const std::array<float, 31>& v)
    {
        micStage = v;
        auto& st = engine.grandMicStage();
        st.setMode (static_cast<int> (v[0] + 0.5f));
        for (int i = 0; i < 5; ++i)
        {
            epi::GrandMicStage::Mic m;
            m.on     = v[1 + i * 6] >= 0.5f;
            m.x      = v[2 + i * 6];
            m.z      = v[3 + i * 6];
            m.h      = v[4 + i * 6];
            m.gainDb = v[5 + i * 6];
            m.pan    = v[6 + i * 6];
            st.setMic (i, m);
        }
    }
    void resetMicStage() { setMicStage (kStageDefaults); }
    const std::array<float, 31>& getMicStage() const { return micStage; }
    static constexpr std::array<float, 31> kStageDefaults {
        0.0f,
        1.0f, -0.5f, 1.2f, 0.6f,  0.0f, -0.7f,
        1.0f,  0.5f, 1.2f, 0.6f,  0.0f,  0.7f,
        0.0f,  0.0f, 2.5f, 1.0f,  0.0f,  0.0f,
        0.0f, -1.2f, 0.4f, 0.3f, -6.0f, -1.0f,
        0.0f,  1.2f, 0.4f, 0.3f, -6.0f,  1.0f };
    void resetCabMods() { setCabMod (kCabDefaults); }
    const std::array<float, 5>& getCabMods() const { return cabMods; }
    static constexpr std::array<float, 5> kCabDefaults { 0.74f, 0.59f, 0.5f, 0.25f, 0.5f };
    // spread, balance, distance, level L, level R -- the calibrated pair.
    static constexpr std::array<float, 5> kMicDefaults { 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };

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
    juce::ValueTree buildModsTree (bool always) const;
    void applyModsTree (const juce::ValueTree& mods);
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
    std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> stringMods = [] {
        std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> a {};
        for (auto& m : a) m = { 1.0f, 1.0f };
        return a;
    }();
    std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> grandMods = [] {
        std::array<std::array<float, 2>, epi::EpiEngine::kNumTines> a {};
        for (auto& m : a) m = { 1.0f, 1.0f };
        return a;
    }();
    std::array<std::array<float, 3>, epi::EpiEngine::kNumTines> pickupMods = [] {
        std::array<std::array<float, 3>, epi::EpiEngine::kNumTines> a {};
        for (auto& m : a) m = { 0.0f, 0.0f, 1.0f };
        return a;
    }();

    std::array<float, 5> cabMods = kCabDefaults;
    std::array<float, 5> micMods = kMicDefaults;
    std::array<float, 31> micStage = kStageDefaults;
    std::array<float, 5> velMap { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

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
