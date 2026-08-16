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

#include "WebEditor.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"

#include <DidgeUIData.h>

#include <cmath>

namespace didge
{
namespace
{
    // ----- Param wiring -----------------------------------------------------
    // Must match the IDs in src/ParameterIDs.h and the PARAM ids used by the
    // JS side (ui/src). A typo here would become a dead control at runtime, so
    // testEditorBindsEveryParameter in tests/test_state.cpp checks this list
    // against the parameter layout.
    constexpr const char* kFloatIds[] = {
        "pressure", "attack", "release", "vibRate", "vibDepth", "breathNoise",
        "decay", "sustain", "velAmount", "humanize",
        "tension", "lipDamp", "embouchure", "bendRange",
        "tractMix", "vowelX", "vowelY", "growl", "growlPitch",
        "tune", "bell", "flare", "texture", "wallDamp", "boreDia",
        "spaceMix", "spaceSize", "outGain",
    };
    constexpr const char* kBoolIds[]   = { "decayOn" };
    constexpr const char* kChoiceIds[] = { "velTarget", "boreProfile", "material",
                                           "exciter" };

    // Design canvas. The JS fit scaler letterbox-scales #plugin to the window;
    // the editor constrains resize to the same aspect so there are no borders.
    constexpr int kDesignW = 1280;
    constexpr int kDesignH = 830;

    // The browser side already animates from requestAnimationFrame at the
    // display's own rate, so this is what actually decides how current the
    // drawing is: at 30 Hz the meters and the spectrum visibly step.
    constexpr int kTelemetryHz = 60;

    epicommon::WebResources& resources()
    {
        static epicommon::WebResources t {
            { DidgeUIData::namedResourceListSize,
              DidgeUIData::namedResourceList,
              DidgeUIData::getNamedResource,
              DidgeUIData::getNamedResourceOriginalFilename },
            "Didge" };
        return t;
    }
}

juce::StringArray WebEditor::boundParameterIds()
{
    juce::StringArray ids;
    for (auto id : kFloatIds)  ids.add (id);
    for (auto id : kBoolIds)   ids.add (id);
    for (auto id : kChoiceIds) ids.add (id);
    return ids;
}

// ============================================================================
WebEditor::WebEditor (::DidgeAudioProcessor& proc)
    : juce::AudioProcessorEditor (&proc),
      didgeProcessor (proc),
      watchdog ({ "__didgeReady", "__didgeMountError" },
                [this] (juce::String diag) { showFallback (diag); })
{
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 7 / 4, kDesignH * 7 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    epicommon::prepareWebViewEnvironment ("Didge");

    auto& apvts = didgeProcessor.getValueTreeState();
    auto& presets = didgeProcessor.getPresetManager();

    juce::WebBrowserComponent::Options options;
    options = options
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("DidgeWebView2"))
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
        .withUserScript ("window.DIDGE_VERSION_STR = 'v" DIDGE_VERSION " · " DIDGE_GIT_BRANCH "';")
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
                         kBoolIds,   (int) std::size (kBoolIds),
                         kChoiceIds, (int) std::size (kChoiceIds));

    webView = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*webView);

    relays.attachAll (apvts);

    setBounds (epicommon::initialEditorSize (kDesignW, kDesignH));

    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
    didgeProcessor.getEngine().setSpectrumEnabled (true);
    watchdog.restart();
    startTimerHz (kTelemetryHz);
}

WebEditor::~WebEditor()
{
    // Stop the analyser costing CPU once nobody is looking at it.
    didgeProcessor.getEngine().setSpectrumEnabled (false);
}

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

void WebEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void WebEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
    if (fallback != nullptr && fallback->isVisible())
        fallback->setBounds (getLocalBounds());
}

bool WebEditor::keyPressed (const juce::KeyPress& k)
{
    // Cmd/Ctrl+R reload works even when the WebView has gone dead.
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

    auto& engine = didgeProcessor.getEngine();

    juce::Array<juce::var> outArr;
    for (int i = 0; i < 2; ++i)
        outArr.add (juce::var (toDb (engine.consumeOutPeak (i))));

    // Bore profile and vocal tract drive the cutaway drawing in the UI, so
    // what is on screen is the geometry the model is actually running.
    juce::Array<juce::var> bore;
    for (int i = 0; i < didge::Bore::kSegments; ++i)
        bore.add (juce::var (engine.vizBoreRadius (i)));

    juce::Array<juce::var> tract;
    for (int i = 0; i < didge::VocalTract::kSections; ++i)
        tract.add (juce::var (engine.vizTractArea (i)));

    // Standing wave and airflow along the bore, plus the lip motion itself.
    // The drawing is built from these rather than from an assumed mode shape,
    // so the nodes sit where the waveguide actually puts them.
    // Wave field: complex amplitude of pressure and of the air's displacement
    // at each segment boundary. Sent as real/imaginary pairs so the page can
    // reconstruct the wave at any instant it likes -- the phase difference
    // between positions is what makes it travel.
    juce::Array<juce::var> waveP, waveD;
    juce::Array<juce::var> press, flowSeg;
    for (int i = 0; i < didge::Bore::kSegments; ++i)
    {
        press.add (juce::var (engine.vizBorePressure (i)));
        flowSeg.add (juce::var (engine.vizBoreFlow (i)));
        waveP.add (juce::var (engine.vizPressureRe (i)));
        waveP.add (juce::var (engine.vizPressureIm (i)));
        waveD.add (juce::var (engine.vizDisplaceRe (i)));
        waveD.add (juce::var (engine.vizDisplaceIm (i)));
    }

    // Rounded to whole decibels: the display cannot show more than that, and
    // it keeps the payload small enough to push at the UI frame rate.
    juce::Array<juce::var> spec, specPk;
    for (int i = 0; i < didge::DidgeEngine::kSpectrumBins; ++i)
    {
        spec.add   (juce::var ((int) std::lround (engine.vizSpectrum (i))));
        specPk.add (juce::var ((int) std::lround (engine.vizSpectrumPeak (i))));
    }

    juce::Array<juce::var> peaks;
    for (int i = 0; i < didge::DidgeEngine::kSpectrumPeaks; ++i)
    {
        juce::DynamicObject::Ptr pk = new juce::DynamicObject();
        pk->setProperty ("f",  engine.vizPeakHz (i));
        pk->setProperty ("db", engine.vizPeakDb (i));
        peaks.add (juce::var (pk.get()));
    }

    juce::Array<juce::var> lipWave;
    for (int i = 0; i < didge::DidgeEngine::kLipTraceLen; ++i)
        lipWave.add (juce::var (engine.vizLipTrace (i)));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("out",        juce::var (outArr));
    root->setProperty ("pressure",   engine.vizPressure());
    root->setProperty ("lipOpen",    engine.vizLipOpen());
    root->setProperty ("flow",       engine.vizFlow());
    root->setProperty ("f0",         engine.vizF0());
    root->setProperty ("toot",       engine.vizToot());
    root->setProperty ("tootActive", engine.vizTootActive());
    root->setProperty ("playing",    engine.anyNoteHeld());
    root->setProperty ("bore",       juce::var (bore));
    root->setProperty ("tract",      juce::var (tract));
    root->setProperty ("waveP",      juce::var (waveP));
    root->setProperty ("waveD",      juce::var (waveD));
    root->setProperty ("press",      juce::var (press));
    root->setProperty ("flowSeg",    juce::var (flowSeg));
    root->setProperty ("lipWave",    juce::var (lipWave));
    root->setProperty ("spec",       juce::var (spec));
    root->setProperty ("specPk",     juce::var (specPk));
    root->setProperty ("peaks",      juce::var (peaks));
    root->setProperty ("meanFlow",   engine.vizMeanFlow());
    root->setProperty ("turb",       engine.vizTurbulence());

    webView->emitEventIfBrowserIsVisible (juce::Identifier { "levels" }, juce::var (root.get()));
}

void WebEditor::emitPresetInfo()
{
    if (webView == nullptr) return;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("name",  didgeProcessor.getPresetManager().getCurrentName());
    obj->setProperty ("dirty", didgeProcessor.isCurrentPresetDirty());
    webView->emitEventIfBrowserIsVisible (juce::Identifier { "presetInfo" }, juce::var (obj.get()));
}

} // namespace didge
