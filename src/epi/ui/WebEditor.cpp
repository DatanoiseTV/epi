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

#include "WebEditor.h"
#include "epi/ui/BoundParameterIds.h"
#include "epi/PluginProcessor.h"
#include "epi/ParameterIDs.h"

#include <EpiUIData.h>

#include <cmath>

namespace epi
{
namespace
{
    // Must match the IDs in src/epi/ParameterIDs.h and the PARAMS ids used by
    // the JS side (ui/epi). A typo here would become a dead control at runtime,
    // so testEditorBindsEveryParameter checks this list against the layout.
    // The bound-parameter lists live in BoundParameterIds.h, which the state
    // suite also reads -- one statement of what the interface reaches.
    using epi::ui::kFloatIds;
    using epi::ui::kChoiceIds;

    constexpr int kDesignW = 1224;
    constexpr int kDesignH = 860;
    constexpr int kTelemetryHz = 60;

    epicommon::WebResources& resources()
    {
        static epicommon::WebResources t {
            { EpiUIData::namedResourceListSize,
              EpiUIData::namedResourceList,
              EpiUIData::getNamedResource,
              EpiUIData::getNamedResourceOriginalFilename },
            "Epi" };
        return t;
    }
}

juce::StringArray WebEditor::boundParameterIds() { return epi::ui::boundParameterIds(); }

// ============================================================================
WebEditor::WebEditor (::EpiAudioProcessor& proc)
    : juce::AudioProcessorEditor (&proc),
      epiProcessor (proc),
      watchdog ({ "__epiReady", "__epiMountError" },
                [this] (juce::String diag) { showFallback (diag); })
{
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 7 / 4, kDesignH * 7 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    epicommon::prepareWebViewEnvironment ("Epi");

    auto& apvts = epiProcessor.getValueTreeState();
    auto& presets = epiProcessor.getPresetManager();

    juce::WebBrowserComponent::Options options;
    options = options
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("EpiWebView2"))
            .withStatusBarDisabled()
            .withBuiltInErrorPageDisabled())
       #else
        .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend)
       #endif
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled (true)
        .withResourceProvider (
            [] (const juce::String& url) { return resources().lookup (url); },
            juce::URL (juce::WebBrowserComponent::getResourceProviderRoot()).getOrigin())
        .withUserScript ("window.EPI_VERSION_STR = 'v" EPI_VERSION " · " EPI_GIT_BRANCH "';")
        .withNativeFunction (juce::Identifier { "reloadUI" },
            [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::MessageManager::callAsync ([this] { reloadWebView(); });
                complete (juce::var());
            })
        .withNativeFunction (juce::Identifier { "listFactoryPresets" },
            [&presets] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::Array<juce::var> arr;
                for (const auto& n : presets.getFactoryNames()) arr.add (juce::var (n));
                complete (juce::var (arr));
            })
        .withNativeFunction (juce::Identifier { "listUserPresets" },
            [&presets] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::Array<juce::var> arr;
                for (const auto& n : presets.getUserNames()) arr.add (juce::var (n));
                complete (juce::var (arr));
            })
        .withNativeFunction (juce::Identifier { "getTineMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (const auto& m : proc.getTineMods())
                {
                    flat.add (juce::var (static_cast<double> (m[0])));
                    flat.add (juce::var (static_cast<double> (m[1])));
                }
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "tine_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setTineMod (static_cast<int> (payload.getProperty ("index", -1)),
                                 static_cast<float> (static_cast<double> (payload.getProperty ("len", 1.0))),
                                 static_cast<float> (static_cast<double> (payload.getProperty ("dia", 1.0))));
            })
        .withEventListener (juce::Identifier { "tine_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetTineMods(); })
        .withNativeFunction (juce::Identifier { "getPickupMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (const auto& m : proc.getPickupMods())
                {
                    flat.add (juce::var (static_cast<double> (m[0])));
                    flat.add (juce::var (static_cast<double> (m[1])));
                    flat.add (juce::var (static_cast<double> (m[2])));
                }
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "pickup_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setPickupMod (static_cast<int> (payload.getProperty ("index", -1)),
                                   static_cast<float> (static_cast<double> (payload.getProperty ("h", 0.0))),
                                   static_cast<float> (static_cast<double> (payload.getProperty ("g", 0.0))),
                                   static_cast<float> (static_cast<double> (payload.getProperty ("s", 1.0))));
            })
        .withEventListener (juce::Identifier { "pickup_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetPickupMods(); })
        .withNativeFunction (juce::Identifier { "getStringMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (const auto& m : proc.getStringMods())
                {
                    flat.add (juce::var (static_cast<double> (m[0])));
                    flat.add (juce::var (static_cast<double> (m[1])));
                }
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "string_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setStringMod (static_cast<int> (payload.getProperty ("index", -1)),
                                   static_cast<float> (static_cast<double> (payload.getProperty ("len", 1.0))),
                                   static_cast<float> (static_cast<double> (payload.getProperty ("dia", 1.0))));
            })
        .withEventListener (juce::Identifier { "string_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetStringMods(); })
        .withNativeFunction (juce::Identifier { "getGrandMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (const auto& m : proc.getGrandMods())
                {
                    flat.add (juce::var (static_cast<double> (m[0])));
                    flat.add (juce::var (static_cast<double> (m[1])));
                }
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "grand_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setGrandMod (static_cast<int> (payload.getProperty ("index", -1)),
                                  static_cast<float> (static_cast<double> (payload.getProperty ("len", 1.0))),
                                  static_cast<float> (static_cast<double> (payload.getProperty ("dia", 1.0))));
            })
        .withEventListener (juce::Identifier { "grand_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetGrandMods(); })
        .withNativeFunction (juce::Identifier { "getCabMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (float v : proc.getCabMods()) flat.add (juce::var (static_cast<double> (v)));
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "cab_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setCabMod ({
                    static_cast<float> (static_cast<double> (payload.getProperty ("box", 0.74))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("cone", 0.59))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("dist", 0.5))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("angle", 0.25))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("susp", 0.5))) });
            })
        .withEventListener (juce::Identifier { "cab_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetCabMods(); })
        .withNativeFunction (juce::Identifier { "getMicMods" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (float v : proc.getMicMods()) flat.add (juce::var (static_cast<double> (v)));
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "mic_mod" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setMicMod ({
                    static_cast<float> (static_cast<double> (payload.getProperty ("spread", 1.0))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("bias", 0.0))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("dist", 0.0))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("lvlL", 1.0))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("lvlR", 1.0))) });
            })
        .withEventListener (juce::Identifier { "mic_mod_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetMicMods(); })
        .withNativeFunction (juce::Identifier { "getMicStage" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (float v : proc.getMicStage()) flat.add (juce::var (static_cast<double> (v)));
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "mic_stage" },
            [&proc = epiProcessor] (juce::var payload)
            {
                if (auto* arr = payload.getProperty ("v", juce::var()).getArray();
                    arr != nullptr && arr->size() == 31)
                {
                    std::array<float, 31> v {};
                    for (int i = 0; i < 31; ++i)
                        v[static_cast<size_t> (i)] = static_cast<float> (static_cast<double> ((*arr)[i]));
                    proc.setMicStage (v);
                }
            })
        .withNativeFunction (juce::Identifier { "getVelMap" },
            [&proc = epiProcessor] (const juce::Array<juce::var>&, auto complete)
            {
                juce::Array<juce::var> flat;
                for (float v : proc.getVelMap()) flat.add (juce::var (static_cast<double> (v)));
                complete (juce::var (flat));
            })
        .withEventListener (juce::Identifier { "vel_map" },
            [&proc = epiProcessor] (juce::var payload)
            {
                proc.setVelMap ({
                    static_cast<float> (static_cast<double> (payload.getProperty ("y0", 0.0))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("y1", 0.25))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("y2", 0.5))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("y3", 0.75))),
                    static_cast<float> (static_cast<double> (payload.getProperty ("y4", 1.0))) });
            })
        .withEventListener (juce::Identifier { "vel_map_reset" },
            [&proc = epiProcessor] (juce::var) { proc.resetVelMap(); })
        .withEventListener (juce::Identifier { "ui_note" },
            [&proc = epiProcessor] (juce::var payload)
            {
                const int note = static_cast<int> (payload.getProperty ("note", -1));
                const bool on = static_cast<bool> (payload.getProperty ("on", false));
                const float vel = static_cast<float> (
                    static_cast<double> (payload.getProperty ("velocity", 0.75)));
                proc.pushUiNote (note, vel, on);
            })
        .withEventListener (juce::Identifier { "preset_prev" },
            [&presets] (juce::var) { presets.previous(); })
        .withEventListener (juce::Identifier { "preset_next" },
            [&presets] (juce::var) { presets.next(); })
        .withEventListener (juce::Identifier { "preset_load" },
            [&presets] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString();
                if (name.isNotEmpty()) presets.loadByName (name);
            })
        .withEventListener (juce::Identifier { "preset_save" },
            [&presets] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString().trim();
                if (name.isNotEmpty()) presets.saveUser (name);
            })
        .withEventListener (juce::Identifier { "preset_delete" },
            [&presets] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString();
                if (name.isNotEmpty()) presets.deleteUser (name);
            });

    relays.addToOptions (options,
                         kFloatIds,  (int) std::size (kFloatIds),
                         nullptr,    0,
                         kChoiceIds, (int) std::size (kChoiceIds));

    webView = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*webView);

    relays.attachAll (apvts);

    setBounds (epicommon::initialEditorSize (kDesignW, kDesignH));

    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
    watchdog.restart();
    startTimerHz (kTelemetryHz);
}

WebEditor::~WebEditor() = default;

void WebEditor::showFallback (const juce::String& jsErrorIfAny)
{
    if (fallback == nullptr)
        fallback = std::make_unique<epicommon::WebViewFallback> ([this] { reloadWebView(); });
    addAndMakeVisible (*fallback);
    fallback->setBounds (getLocalBounds());
    fallback->toFront (false);
    fallback->setDiagnostic (jsErrorIfAny);
}

void WebEditor::reloadWebView()
{
    if (webView == nullptr) return;
    if (fallback != nullptr) fallback->setVisible (false);
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
    watchdog.restart();
}

void WebEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void WebEditor::resized()
{
    if (webView != nullptr) webView->setBounds (getLocalBounds());
    if (fallback != nullptr && fallback->isVisible()) fallback->setBounds (getLocalBounds());
}

bool WebEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getTextCharacter() == 'r')
    {
        reloadWebView();
        return true;
    }
    return false;
}

void WebEditor::timerCallback()
{
    if (webView != nullptr)
    {
        watchdog.tick (*webView, kTelemetryHz);
        if (watchdog.isHealthy() && fallback != nullptr && fallback->isVisible())
            fallback->setVisible (false);
    }

    emitLevels();
    emitPresetInfo();
}

void WebEditor::emitLevels()
{
    if (webView == nullptr) return;

    auto toDb = [] (float lin) -> float
    {
        if (lin <= 1.0e-5f) return -90.0f;
        return 20.0f * std::log10 (lin);
    };

    auto& engine = epiProcessor.getEngine();

    juce::Array<juce::var> outArr;
    for (int i = 0; i < 2; ++i)
        outArr.add (juce::var (toDb (engine.consumeOutPeak (i))));

    // The field the tine is actually moving through, straight from the model.
    // The drawing is the computed magnetic profile, not an illustration of one,
    // so moving the pickup height on screen moves the tine along the real curve.
    juce::Array<juce::var> profile;
    for (int i = 0; i < epi::EpiEngine::kFieldPoints; ++i)
        profile.add (juce::var (engine.vizField (i)));

    // The tine's own motion, decimated to about four cycles. Without this the
    // interface can only invent a wobble: at sixty telemetry ticks a second it
    // cannot otherwise represent an eighty-hertz waveform, let alone a treble
    // note.
    juce::Array<juce::var> trace;
    for (int i = 0; i < epi::EpiEngine::kTraceLen; ++i)
        trace.add (juce::var (engine.vizTrace (i)));

    // The whole harp: every tine's peak motion this frame, in microns, so the
    // interface can draw the instrument instead of one part of it.
    juce::Array<juce::var> harp;
    for (int i = 0; i < epi::EpiEngine::kNumTines; ++i)
        harp.add (juce::var (engine.vizTineTip (i) * 1.0e6f));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("out",     juce::var (outArr));
    root->setProperty ("trace",   juce::var (trace));
    root->setProperty ("noteHz",  engine.vizNoteHz());
    root->setProperty ("strikes", engine.vizStrikes());
    root->setProperty ("field",   juce::var (profile));
    root->setProperty ("tip",     engine.vizTipDisplacement());
    root->setProperty ("flux",    engine.vizFlux());
    root->setProperty ("offset",  engine.vizPickupOffset());
    root->setProperty ("vibL",    engine.vizVibratoL());
    root->setProperty ("vibR",    engine.vizVibratoR());
    root->setProperty ("voices",  engine.activeVoices());
    juce::Array<juce::var> keys;
    for (int w = 0; w < epi::EpiEngine::kKeyWords; ++w)
        keys.add (juce::var (static_cast<double> (engine.vizKeys (w))));

    root->setProperty ("harp",    juce::var (harp));
    root->setProperty ("keys",    juce::var (keys));
    root->setProperty ("pedal",   engine.vizPedal());
    root->setProperty ("lastNote", engine.vizLastNote());
    root->setProperty ("loNote",  epi::EpiEngine::kLoNote);

    webView->emitEventIfBrowserIsVisible (juce::Identifier { "levels" }, juce::var (root.get()));
}

void WebEditor::emitPresetInfo()
{
    if (webView == nullptr) return;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("name",  epiProcessor.getPresetManager().getCurrentName());
    obj->setProperty ("dirty", epiProcessor.isCurrentPresetDirty());
    webView->emitEventIfBrowserIsVisible (juce::Identifier { "presetInfo" }, juce::var (obj.get()));
}

} // namespace epi
