/*
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
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The parts of a JUCE 8 WebView editor that have nothing to do with which
// instrument is being edited: serving the bundled page out of BinaryData,
// owning the parameter relays in the one order that is safe, and noticing
// when the WebView has gone dead. Each plugin composes these; what remains in
// its own editor is the option chain and the telemetry it pushes, both of
// which are genuinely product-shaped.
namespace epicommon
{

// ---------------------------------------------------------------------------
// Serving the UI bundle out of BinaryData.
//
// juce_add_binary_data puts its accessors in a per-target namespace, and a
// namespace cannot be a template argument, so the four entry points are passed
// in as a descriptor instead.
// ---------------------------------------------------------------------------
struct BinaryDataSource
{
    int listSize = 0;
    const char* const* list = nullptr;
    const char* (*getResource) (const char* name, int& size) = nullptr;
    const char* (*getOriginalFilename) (const char* name) = nullptr;
};

class WebResources
{
public:
    // `productName` only appears in the not-found message on stderr, which is
    // how a terminal-launched standalone reports the blank-window failure.
    WebResources (BinaryDataSource source, juce::String productName);

    std::optional<juce::WebBrowserComponent::Resource> lookup (const juce::String& url) const;

    static juce::String mimeForName (const juce::String& name);

private:
    struct Entry { juce::String filename; const char* data; int size; };
    std::vector<Entry> entries;
    juce::String product;
};

// ---------------------------------------------------------------------------
// Parameter relays.
//
// CRITICAL: relays are WebViewLifetimeListeners on the browser, and the
// browser's destructor walks its listener list -- so every relay must outlive
// the browser. Keeping them all in this one object means an editor only has to
// declare it BEFORE its WebBrowserComponent member, rather than getting the
// declaration order of three separate vectors right.
//
// Usage, in this order:
//   relays.addToOptions (options, floatIds, boolIds, choiceIds);
//   webView = std::make_unique<WebBrowserComponent> (options);   // after
//   relays.attachAll (apvts);                                    // after that
// ---------------------------------------------------------------------------
class WebParamRelays
{
public:
    // Registers one relay per id and folds them all into `options`. Ids are
    // taken by span-like pointer+count so a plugin can keep its own
    // constexpr arrays.
    void addToOptions (juce::WebBrowserComponent::Options& options,
                       const char* const* floatIds,  int numFloat,
                       const char* const* boolIds,   int numBool,
                       const char* const* choiceIds, int numChoice);

    // Binds each relay to its parameter. Must run after the browser exists.
    // Ids with no matching parameter are skipped, and reported by
    // `missingParameters()` so a typo can fail a test instead of silently
    // producing a dead control.
    void attachAll (juce::AudioProcessorValueTreeState& apvts);

    const juce::StringArray& missingParameters() const { return missing; }

    // Every id this editor claims, in the order it was registered. Lets a test
    // assert that the editor's id lists and the parameter layout agree.
    juce::StringArray allIds() const;

private:
    struct SliderBinding { std::unique_ptr<juce::WebSliderRelay>       relay; std::unique_ptr<juce::WebSliderParameterAttachment>       attach; };
    struct ToggleBinding { std::unique_ptr<juce::WebToggleButtonRelay> relay; std::unique_ptr<juce::WebToggleButtonParameterAttachment> attach; };
    struct ComboBinding  { std::unique_ptr<juce::WebComboBoxRelay>     relay; std::unique_ptr<juce::WebComboBoxParameterAttachment>     attach; };

    std::vector<SliderBinding> sliders;
    std::vector<ToggleBinding> toggles;
    std::vector<ComboBinding>  combos;
    juce::StringArray sliderIds, toggleIds, comboIds, missing;
};

// ---------------------------------------------------------------------------
// The panel shown when the page never comes up. A wedged WebView is otherwise
// an unexplained grey rectangle, with the actual cause only visible in a
// console nobody is watching.
// ---------------------------------------------------------------------------
class WebViewFallback : public juce::Component
{
public:
    explicit WebViewFallback (std::function<void()> onReload);

    void setDiagnostic (const juce::String& s) { diagText = s; repaint(); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton reloadButton;
    juce::String     diagText;
    std::function<void()> reloadCallback;
};

// ---------------------------------------------------------------------------
// Health watchdog. The page sets `window.<readyGlobal> = true` once it has
// mounted, and `window.<errorGlobal>` if mounting threw. This polls for either
// and gives up after a deadline.
// ---------------------------------------------------------------------------
class WebViewWatchdog
{
public:
    struct Globals { juce::String ready, error; };

    WebViewWatchdog (Globals g, std::function<void (juce::String)> onWedged);

    // Call on navigation. Clears any previous verdict and re-arms the deadline.
    void restart();

    // Call once per editor timer tick, at `tickHz`.
    void tick (juce::WebBrowserComponent& webView, int tickHz);

    bool isHealthy() const { return healthy; }

private:
    void poll (juce::WebBrowserComponent& webView);

    Globals globals;
    std::function<void (juce::String)> wedgedCallback;
    bool healthy = false;
    int  ticksRemaining = 0;
    int  pollEvery = 1;
};

// ---------------------------------------------------------------------------
// Platform quirks that every WebView editor in this repo has to work around,
// and the initial-size rule shared by both products.
// ---------------------------------------------------------------------------

// Linux only: forces GTK onto X11 (JUCE 8's WebKit is an X11 client, so the
// XEmbed reparent into the host window fails under Wayland) and points JUCE's
// WebKit helper extraction at an exec-allowed, user-owned directory, since
// /tmp is mounted noexec on a fair number of distributions. No-op elsewhere.
void prepareWebViewEnvironment (const juce::String& productFolderName);

// The design canvas at 1:1 when the display can hold it, otherwise the largest
// box of the same aspect that fits 92% of the primary display.
juce::Rectangle<int> initialEditorSize (int designW, int designH);

} // namespace epicommon
