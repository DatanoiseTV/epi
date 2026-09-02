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
#include "epi/ui/UiBridge.h"
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
    const auto bridge = epi::ui::makeBridge (epiProcessor);

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
        // CharPointer_UTF8, not a bare literal: juce::String decodes a
        // const char* as Latin-1, so the separator would be re-encoded and
        // the header would read "v0.9.0 A· main".
        .withUserScript (juce::String (juce::CharPointer_UTF8 (
            "window.EPI_VERSION_STR = 'v" EPI_VERSION " · " EPI_GIT_BRANCH "';")))
        .withNativeFunction (juce::Identifier { "reloadUI" },
            [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::MessageManager::callAsync ([this] { reloadWebView(); });
                complete (juce::var());
            })
        ;

    // Every other native function and event listener comes from the shared
    // bridge, so the plugin and the headless host answer the interface with
    // one implementation rather than two that drift.
    for (const auto& e : bridge.natives)
    {
        auto fn = e.second;
        options = options.withNativeFunction (e.first,
            [fn] (const juce::Array<juce::var>& args,
                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (fn (args.isEmpty() ? juce::var() : juce::var (args)));
            });
    }
    for (const auto& e : bridge.listeners)
    {
        auto fn = e.second;
        options = options.withEventListener (e.first,
            [fn] (juce::var payload) { fn (payload); });
    }

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
    webView->emitEventIfBrowserIsVisible (juce::Identifier { "levels" },
                                          epi::ui::buildLevels (epiProcessor));
}

void WebEditor::emitPresetInfo()
{
    if (webView == nullptr) return;
    webView->emitEventIfBrowserIsVisible (juce::Identifier { "presetInfo" },
                                          epi::ui::buildPresetInfo (epiProcessor));
}

} // namespace epi
