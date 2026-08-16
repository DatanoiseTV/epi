/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  State round-trip + preset tests against the real AudioProcessor.
*/

#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "ui/WebEditor.h"
#include "common/PresetManager.h"

#include <iostream>

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  "     \
                      << msg << std::endl;                                  \
        }                                                                   \
    } while (0)

static void setParam (DidgeAudioProcessor& p, const char* id, float realValue)
{
    if (auto* rp = p.getValueTreeState().getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
}

static float getParam (DidgeAudioProcessor& p, const char* id)
{
    if (auto* rp = p.getValueTreeState().getParameter (id))
        return rp->convertFrom0to1 (rp->getValue());
    return -999.0f;
}

// Render a few blocks with a held note, checking the output stays finite.
static void runAudio (DidgeAudioProcessor& p, int blocks = 8)
{
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 38, 0.8f), 0);
        buf.clear();
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                CHECK (std::isfinite (buf.getSample (ch, i)), "non-finite audio out");
    }
}

static void testStateRoundTrip()
{
    DidgeAudioProcessor a;
    setParam (a, didge::ids::pressure, 0.81f);
    setParam (a, didge::ids::vowelX, 0.72f);
    setParam (a, didge::ids::bell, 0.93f);
    setParam (a, didge::ids::growl, 0.44f);
    setParam (a, didge::ids::outGain, -6.5f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    DidgeAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (std::abs (getParam (b, didge::ids::pressure) - 0.81f) < 1.0e-3f, "pressure did not round-trip");
    CHECK (std::abs (getParam (b, didge::ids::vowelX)   - 0.72f) < 1.0e-3f, "vowelX did not round-trip");
    CHECK (std::abs (getParam (b, didge::ids::bell)     - 0.93f) < 1.0e-3f, "bell did not round-trip");
    CHECK (std::abs (getParam (b, didge::ids::growl)    - 0.44f) < 1.0e-3f, "growl did not round-trip");
    CHECK (std::abs (getParam (b, didge::ids::outGain)  + 6.5f)  < 1.0e-2f, "outGain did not round-trip");
}

// Every parameter, not a hand-picked few: a control that silently fails to
// save is indistinguishable from one that does nothing.
static void testAllParametersRoundTrip()
{
    DidgeAudioProcessor a;

    int n = 0;
    std::vector<std::pair<juce::String, float>> saved;
    for (auto* p : a.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto range = rp->getNormalisableRange();
            const float v = range.snapToLegalValue (
                range.convertFrom0to1 (0.3f + 0.4f * ((n % 5) / 4.0f)));
            rp->setValueNotifyingHost (range.convertTo0to1 (v));
            saved.push_back ({ rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()) });
            ++n;
        }
    CHECK (n >= 25, "expected the full parameter set, saw " << n);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    DidgeAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    for (const auto& [id, want] : saved)
    {
        auto* rp = b.getValueTreeState().getParameter (id);
        const float got = rp != nullptr ? rp->convertFrom0to1 (rp->getValue()) : -9999.0f;
        CHECK (std::abs (got - want) <= 1.0e-3f * juce::jmax (1.0f, std::abs (want)),
               "parameter " << id << " did not survive a save and reload: saved "
                            << want << ", restored " << got);
    }
}

// The preset name is not a parameter, so it has to be stored with the state
// explicitly. Without it a session reloads with the user's values under some
// other preset's name, shown as unmodified, which reads as lost work.
static void testPresetNamePersists()
{
    DidgeAudioProcessor a;
    const auto names = a.getPresetManager().getFactoryNames();
    CHECK (names.size() > 1, "need more than one factory preset for this test");

    a.getPresetManager().loadByName (names[1]);
    setParam (a, didge::ids::growl, 0.66f);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    DidgeAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    CHECK (b.getPresetManager().getCurrentName() == names[1],
           "preset name lost on reload: expected \"" << names[1]
           << "\", got \"" << b.getPresetManager().getCurrentName() << "\"");
    CHECK (std::abs (getParam (b, didge::ids::growl) - 0.66f) < 1.0e-3f,
           "edits made after loading a preset were lost");
}

// Parameter ORDER is part of the plugin's published interface, not just the
// set of ids. Hosts address parameters by position as well as by id, so
// inserting a new one in the middle shifts every parameter after it and a
// session saved by an earlier build reloads with each value landing on the
// wrong control -- heard as a transposed instrument that drops notes and
// retriggers, with nothing wrong in the audio path. New parameters go at the
// end. This pins the prefix that shipped so the mistake cannot recur silently.
static void testParameterOrderIsStable()
{
    static const char* const published[] = {
        "pressure", "attack", "release", "vibRate", "vibDepth", "breathNoise",
        "decayOn", "decay", "sustain", "velTarget", "velAmount", "humanize",
        "tension", "lipDamp", "embouchure", "bendRange", "tractMix", "vowelX",
        "vowelY", "growl", "growlPitch", "tune", "bell", "flare",
        "texture", "wallDamp", "boreProfile", "material", "spaceMix", "spaceSize",
        "outGain", "exciter", "boreDia",
    };

    DidgeAudioProcessor p;
    juce::StringArray order;
    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            order.add (rp->getParameterID());

    if (std::getenv ("DIDGE_DUMP_ORDER") != nullptr)
    {
        for (int i = 0; i < order.size(); ++i)
            std::cout << (i % 6 == 0 ? "\n        " : "") << "\"" << order[i] << "\", ";
        std::cout << std::endl;
    }
    const int n = (int) (sizeof (published) / sizeof (published[0]));
    CHECK (order.size() >= n,
           "parameters went missing: expected at least " << n << ", got " << order.size());

    for (int i = 0; i < juce::jmin (n, order.size()); ++i)
        CHECK (order[i] == published[i],
               "parameter " << i << " moved: expected \"" << published[i]
               << "\", found \"" << order[i]
               << "\". Append new parameters at the end of the layout instead.");
}

// The editor's id lists and the parameter layout are two hand-written copies
// of the same contract. Nothing links them at compile time: a parameter the
// editor forgets is a control that silently does nothing, and an id the editor
// invents is a relay with no parameter behind it. Both are indistinguishable
// from a working plugin until someone turns that particular knob.
static void testEditorBindsEveryParameter()
{
    DidgeAudioProcessor p;

    juce::StringArray layout;
    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            layout.add (rp->getParameterID());

    const auto bound = didge::WebEditor::boundParameterIds();

    for (const auto& id : layout)
        CHECK (bound.contains (id),
               "parameter \"" << id << "\" exists but the editor never binds it, "
               "so its control does nothing. Add it to kFloatIds/kBoolIds/"
               "kChoiceIds in src/ui/WebEditor.cpp.");

    for (const auto& id : bound)
        CHECK (layout.contains (id),
               "the editor binds \"" << id << "\", which is not a parameter. "
               "Check for a typo against src/ParameterIDs.h.");

    juce::StringArray seen;
    for (const auto& id : bound)
    {
        CHECK (! seen.contains (id),
               "the editor binds \"" << id << "\" more than once");
        seen.add (id);
    }
}

// Preset names reach the filesystem as filenames. A name carrying a path
// separator either fails to write or escapes the preset directory entirely,
// so it is folded before it gets there.
static void testPresetNamesAreSafeAsFilenames()
{
    using epicommon::PresetManager;

    // Separators fold to underscores, then the leading dots go, so the result
    // cannot climb out of the preset directory or hide itself.
    CHECK (PresetManager::toFileName ("../../etc/passwd") == "_.._etc_passwd",
           "a relative path in a preset name was not neutralised: got \""
           << PresetManager::toFileName ("../../etc/passwd") << "\"");
    CHECK (! PresetManager::toFileName ("a/b\\c:d*e?f\"g<h>i|j").containsAnyOf ("/\\:*?\"<>|"),
           "reserved filename characters survived sanitising");
    CHECK (PresetManager::toFileName ("Mellow Tine") == "Mellow Tine",
           "an ordinary name was altered");
    CHECK (PresetManager::toFileName ("   ") == "Untitled",
           "a name that sanitises away did not fall back to a usable filename");
    CHECK (PresetManager::toFileName (".hidden") == "hidden",
           "a leading dot was kept, which hides the preset file on Unix");
}

static void testFactoryPresets()
{
    DidgeAudioProcessor p;
    auto& pm = p.getPresetManager();
    const auto names = pm.getFactoryNames();
    CHECK (names.size() >= 8, "expected a full factory bank");

    for (int i = 0; i < names.size(); ++i)
    {
        pm.loadFactory (i);
        CHECK (pm.getCurrentName() == names[i], "preset name did not update on load");
        CHECK (! p.isCurrentPresetDirty(), "freshly loaded preset reported dirty");
        runAudio (p, 4);
    }
}

static void testDirtyTracking()
{
    DidgeAudioProcessor p;
    p.getPresetManager().loadFactory (0);
    CHECK (! p.isCurrentPresetDirty(), "preset dirty right after load");

    setParam (p, didge::ids::growl, 0.77f);
    CHECK (p.isCurrentPresetDirty(), "editing a parameter did not mark the preset dirty");

    p.getPresetManager().loadFactory (0);
    CHECK (! p.isCurrentPresetDirty(), "reloading did not clear the dirty flag");
}

static void testBusLayouts()
{
    DidgeAudioProcessor p;
    // An instrument takes no audio input; stereo and mono outputs are valid.
    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.isBusesLayoutSupported (stereo), "stereo out should be supported");

    juce::AudioProcessor::BusesLayout withInput;
    withInput.inputBuses.add (juce::AudioChannelSet::stereo());
    withInput.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (! p.isBusesLayoutSupported (withInput), "an audio input bus should be rejected");
}

static void testMidiDrivesTheInstrument()
{
    DidgeAudioProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);

    // Silence before any note.
    {
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock (buf, midi);
        CHECK (buf.getMagnitude (0, 512) < 1.0e-4f, "instrument made sound before any note-on");
    }

    // Held note must produce sound.
    float peak = 0.0f;
    for (int b = 0; b < 120; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 38, 0.8f), 0);
        buf.clear();
        p.processBlock (buf, midi);
        if (b > 40) peak = juce::jmax (peak, buf.getMagnitude (0, 512));
    }
    CHECK (peak > 0.01f, "note-on did not make the instrument speak");

    // After note-off it must fall silent again.
    for (int b = 0; b < 300; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, 38), 0);
        buf.clear();
        p.processBlock (buf, midi);
    }
    CHECK (buf.getMagnitude (0, 512) < 1.0e-3f, "instrument kept sounding after note-off");
}

int main()
{
    testStateRoundTrip();
    testAllParametersRoundTrip();
    testPresetNamePersists();
    testParameterOrderIsStable();
    testEditorBindsEveryParameter();
    testPresetNamesAreSafeAsFilenames();
    testFactoryPresets();
    testDirtyTracking();
    testBusLayouts();
    testMidiDrivesTheInstrument();

    if (failures == 0)
        std::cout << "All state tests passed." << std::endl;
    else
        std::cout << failures << " state test(s) FAILED." << std::endl;
    return failures == 0 ? 0 : 1;
}
