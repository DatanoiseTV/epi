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

#include <juce_core/juce_core.h>

#include <functional>
#include <utility>
#include <vector>

class EpiAudioProcessor;

namespace epi::ui
{

// Everything the interface can ask of the host, independent of how it asked.
//
// The plugin reaches this through a WebView's native integration; the headless
// host reaches it through HTTP. Both arrive as a juce::var payload and a name,
// so the handlers themselves need to know about neither -- which is the point:
// there is one implementation of "what a workshop edit does", not two that
// drift apart the first time one of them gains a field.
struct Bridge
{
    // Fire-and-forget: the interface telling the host something changed.
    std::vector<std::pair<juce::Identifier, std::function<void (const juce::var&)>>> listeners;
    // Request/response. Every current one ignores its arguments and returns a
    // flat array, but the signature carries them so that need not stay true.
    std::vector<std::pair<juce::Identifier, std::function<juce::var (const juce::var&)>>> natives;

    const std::function<void (const juce::var&)>* findListener (const juce::String& name) const;
    const std::function<juce::var (const juce::var&)>* findNative (const juce::String& name) const;
};

// The handlers capture the processor by reference, so it must outlive the
// Bridge -- true for both hosts, where it owns them.
Bridge makeBridge (EpiAudioProcessor&);

// The telemetry frame the interface draws from, and the preset line above it.
juce::var buildLevels (EpiAudioProcessor&);
juce::var buildPresetInfo (EpiAudioProcessor&);

} // namespace epi::ui
