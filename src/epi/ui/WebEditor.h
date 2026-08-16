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

#include "../common/WebViewSupport.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

class EpiAudioProcessor;

namespace epi
{

// JUCE 8 WebView-based editor. Hosts a WebBrowserComponent that loads the
// vendored HTML/CSS/JSX bundle from BinaryData; every APVTS parameter is
// two-way bound to its matching DOM control through the relay/attachment pair
// for its type. Live metering, the pickup's own field profile and the tine's
// position in it, and preset state are pushed to the JS side via
// emitEventIfBrowserIsVisible on the UI timer.
//
// The parts that are not specific to a didgeridoo — serving the bundle,
// owning the relays, and noticing a dead WebView — live in
// common/WebViewSupport.h and are shared with the other plugins here.
class WebEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit WebEditor (::EpiAudioProcessor& proc);
    ~WebEditor() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override;

    // The parameter ids this editor claims. Exposed so a test can assert the
    // list and the parameter layout agree — a typo here is otherwise a dead
    // control at runtime, not a compile error.
    static juce::StringArray boundParameterIds();

private:
    void timerCallback() override;
    void emitLevels();
    void emitPresetInfo();
    void reloadWebView();
    void showFallback (const juce::String& jsErrorIfAny);

    ::EpiAudioProcessor& epiProcessor;

    // CRITICAL: the relays are WebViewLifetimeListeners on the browser, and
    // the browser's destructor walks its listener list — so they must outlive
    // it. Members destruct in reverse declaration order, so `relays` is
    // declared BEFORE `webView`.
    epicommon::WebParamRelays relays;
    std::unique_ptr<juce::WebBrowserComponent> webView;

    epicommon::WebViewWatchdog watchdog;
    std::unique_ptr<epicommon::WebViewFallback> fallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebEditor)
};

} // namespace epi
