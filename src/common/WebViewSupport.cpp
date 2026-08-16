/*
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#include "WebViewSupport.h"

#include <cstddef>
#include <iostream>

namespace epicommon
{

// ===========================================================================
// WebResources
// ===========================================================================
WebResources::WebResources (BinaryDataSource source, juce::String productName)
    : product (std::move (productName))
{
    if (source.list == nullptr || source.getResource == nullptr
        || source.getOriginalFilename == nullptr)
        return;

    for (int i = 0; i < source.listSize; ++i)
    {
        const char* name = source.list[i];
        int size = 0;
        const char* data = source.getResource (name, size);
        if (data == nullptr) continue;
        entries.push_back ({ juce::String (source.getOriginalFilename (name)), data, size });
    }
}

juce::String WebResources::mimeForName (const juce::String& name)
{
    if (name.endsWith (".html"))  return "text/html";
    if (name.endsWith (".css"))   return "text/css";
    if (name.endsWith (".js") ||
        name.endsWith (".jsx") ||
        name.endsWith (".mjs"))   return "application/javascript";
    if (name.endsWith (".svg"))   return "image/svg+xml";
    if (name.endsWith (".png"))   return "image/png";
    if (name.endsWith (".json"))  return "application/json";
    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource>
WebResources::lookup (const juce::String& url) const
{
    juce::String name = url.startsWithChar ('/') ? url.substring (1) : url;
    if (name.isEmpty()) name = "index.html";
    const auto slash = name.lastIndexOfChar ('/');
    if (slash >= 0) name = name.substring (slash + 1);
    const auto q = name.indexOfChar ('?');
    if (q >= 0) name = name.substring (0, q);

    for (const auto& e : entries)
    {
        if (e.filename == name)
        {
            juce::WebBrowserComponent::Resource r;
            r.data.assign (reinterpret_cast<const std::byte*> (e.data),
                           reinterpret_cast<const std::byte*> (e.data) + (size_t) e.size);
            r.mimeType = mimeForName (name);
            return r;
        }
    }

    // 404: surfaced on stderr so a terminal-launched standalone shows
    // missing-resource bugs (the blank-window failure class) directly.
    std::cerr << product << " WebView: resource not found — \"" << url
              << "\" (looked up as \"" << name << "\")" << std::endl;
    return std::nullopt;
}

// ===========================================================================
// WebParamRelays
// ===========================================================================
void WebParamRelays::addToOptions (juce::WebBrowserComponent::Options& options,
                                   const char* const* floatIds,  int numFloat,
                                   const char* const* boolIds,   int numBool,
                                   const char* const* choiceIds, int numChoice)
{
    sliders.reserve (static_cast<size_t> (numFloat));
    toggles.reserve (static_cast<size_t> (numBool));
    combos .reserve (static_cast<size_t> (numChoice));

    for (int i = 0; i < numFloat; ++i)
    {
        SliderBinding b;
        b.relay = std::make_unique<juce::WebSliderRelay> (juce::String (floatIds[i]));
        options = options.withOptionsFrom (*b.relay);
        sliders.push_back (std::move (b));
        sliderIds.add (floatIds[i]);
    }
    for (int i = 0; i < numBool; ++i)
    {
        ToggleBinding b;
        b.relay = std::make_unique<juce::WebToggleButtonRelay> (juce::String (boolIds[i]));
        options = options.withOptionsFrom (*b.relay);
        toggles.push_back (std::move (b));
        toggleIds.add (boolIds[i]);
    }
    for (int i = 0; i < numChoice; ++i)
    {
        ComboBinding b;
        b.relay = std::make_unique<juce::WebComboBoxRelay> (juce::String (choiceIds[i]));
        options = options.withOptionsFrom (*b.relay);
        combos.push_back (std::move (b));
        comboIds.add (choiceIds[i]);
    }
}

void WebParamRelays::attachAll (juce::AudioProcessorValueTreeState& apvts)
{
    missing.clear();

    auto rangedParam = [&apvts] (const juce::String& id)
    {
        return dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id));
    };

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        if (auto* p = rangedParam (sliderIds[(int) i]))
            sliders[i].attach = std::make_unique<juce::WebSliderParameterAttachment> (
                *p, *sliders[i].relay, apvts.undoManager);
        else
            missing.add (sliderIds[(int) i]);
    }
    for (size_t i = 0; i < toggles.size(); ++i)
    {
        if (auto* p = rangedParam (toggleIds[(int) i]))
            toggles[i].attach = std::make_unique<juce::WebToggleButtonParameterAttachment> (
                *p, *toggles[i].relay, apvts.undoManager);
        else
            missing.add (toggleIds[(int) i]);
    }
    for (size_t i = 0; i < combos.size(); ++i)
    {
        if (auto* p = rangedParam (comboIds[(int) i]))
            combos[i].attach = std::make_unique<juce::WebComboBoxParameterAttachment> (
                *p, *combos[i].relay, apvts.undoManager);
        else
            missing.add (comboIds[(int) i]);
    }
}

juce::StringArray WebParamRelays::allIds() const
{
    juce::StringArray ids = sliderIds;
    ids.addArray (toggleIds);
    ids.addArray (comboIds);
    return ids;
}

// ===========================================================================
// WebViewFallback
// ===========================================================================
WebViewFallback::WebViewFallback (std::function<void()> onReload)
    : reloadCallback (std::move (onReload))
{
    reloadButton.setButtonText ("Reload UI");
    reloadButton.onClick = [this] { if (reloadCallback) reloadCallback(); };
    addAndMakeVisible (reloadButton);
}

void WebViewFallback::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07090f));
    auto bounds = getLocalBounds().reduced (40);
    auto card = bounds.withSizeKeepingCentre (520, 280);
    g.setColour (juce::Colour (0xff11151f));
    g.fillRoundedRectangle (card.toFloat(), 12.0f);
    g.setColour (juce::Colour (0xff2a3350));
    g.drawRoundedRectangle (card.toFloat(), 12.0f, 1.0f);

    auto inner = card.reduced (24);
    g.setColour (juce::Colour (0xff62e6ff));
    g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    g.drawText ("UI failed to load", inner.removeFromTop (24), juce::Justification::topLeft);

    inner.removeFromTop (10);
    g.setColour (juce::Colour (0xffb9c2d8));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawFittedText (
        "The WebView didn't reach its ready signal within 4 seconds. "
        "Common causes: the OS killed the WebView content process, a "
        "stale browser cache, or a JS error in a script tag. Reload "
        "retries the navigation; if that doesn't help, fully quit the "
        "host and reopen.",
        inner.removeFromTop (90), juce::Justification::topLeft, 5);

    inner.removeFromTop (8);
    if (! diagText.isEmpty())
    {
        g.setColour (juce::Colour (0xffff8888));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                 11.0f, juce::Font::plain)));
        g.drawFittedText (diagText, inner.removeFromTop (80),
                          juce::Justification::topLeft, 6);
    }
}

void WebViewFallback::resized()
{
    auto bounds = getLocalBounds().reduced (40)
                      .withSizeKeepingCentre (520, 280)
                      .reduced (24);
    reloadButton.setBounds (bounds.removeFromBottom (32).withWidth (120));
}

// ===========================================================================
// WebViewWatchdog
// ===========================================================================
WebViewWatchdog::WebViewWatchdog (Globals g, std::function<void (juce::String)> onWedged)
    : globals (std::move (g)), wedgedCallback (std::move (onWedged))
{
}

void WebViewWatchdog::restart()
{
    healthy = false;
    ticksRemaining = -1;   // armed; the first tick sizes the deadline from tickHz
    pollEvery = 1;
}

void WebViewWatchdog::tick (juce::WebBrowserComponent& webView, int tickHz)
{
    if (ticksRemaining == 0) return;

    if (ticksRemaining < 0)
    {
        // Four seconds to come up, polled about eight times a second.
        ticksRemaining = juce::jmax (1, tickHz * 4);
        pollEvery      = juce::jmax (1, tickHz / 8);
    }

    --ticksRemaining;
    if ((ticksRemaining % pollEvery) == 0)
        poll (webView);

    if (ticksRemaining == 0 && ! healthy && wedgedCallback)
        wedgedCallback ({});
}

void WebViewWatchdog::poll (juce::WebBrowserComponent& webView)
{
    const auto script = "(function(){"
                        "  if (window." + globals.ready + " === true) return 'ready';"
                        "  if (window." + globals.error + ") return 'mount-error:' + window."
                                        + globals.error + ";"
                        "  return 'pending';"
                        "})()";

    webView.evaluateJavascript (script,
        [this] (juce::WebBrowserComponent::EvaluationResult result)
        {
            const juce::var* v = result.getResult();
            if (v == nullptr || ! v->isString())
                return;

            const auto s = v->toString();
            if (s == "ready")
            {
                healthy = true;
                ticksRemaining = 0;
            }
            else if (s.startsWith ("mount-error:") && wedgedCallback)
            {
                ticksRemaining = 0;
                wedgedCallback ("Page mount threw:\n" + s.substring (12));
            }
        });
}

// ===========================================================================
// Platform quirks / sizing
// ===========================================================================
void prepareWebViewEnvironment ([[maybe_unused]] const juce::String& productFolderName)
{
   #if JUCE_LINUX
    ::setenv ("GDK_BACKEND", "x11", 0);

    if (::getenv ("TMPDIR") == nullptr)
    {
        const auto leaf = productFolderName.toLowerCase();
        juce::File chosen;

        if (const auto* xdg = ::getenv ("XDG_RUNTIME_DIR"))
        {
            juce::File xdgDir { juce::String (xdg) };
            if (xdgDir.isDirectory())
            {
                chosen = xdgDir.getChildFile (leaf);
                chosen.createDirectory();
            }
        }
        if (chosen == juce::File())
        {
            chosen = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile (".cache").getChildFile (leaf);
            chosen.createDirectory();
        }
        ::setenv ("TMPDIR", chosen.getFullPathName().toRawUTF8(), 0);
    }
   #endif
}

juce::Rectangle<int> initialEditorSize (int designW, int designH)
{
    int w = designW, h = designH;

    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = disp->userBounds;
        const int maxW = juce::roundToInt ((double) area.getWidth()  * 0.92);
        const int maxH = juce::roundToInt ((double) area.getHeight() * 0.92);

        w = juce::jmin (designW, maxW);
        h = juce::roundToInt ((double) w * designH / designW);
        if (h > maxH)
        {
            h = maxH;
            w = juce::roundToInt ((double) h * designW / designH);
        }
    }

    return { 0, 0, w, h };
}

} // namespace epicommon
