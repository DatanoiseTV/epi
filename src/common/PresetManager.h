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

#include <functional>
#include <utility>
#include <vector>

namespace epicommon
{

// Factory and user presets on top of an APVTS, shared by every plugin in this
// repository. Nothing here knows which instrument it is managing: the product
// identity and the factory bank are handed in by the plugin that owns it.
//
// Factory presets are defined in code; user presets are XML in the platform's
// application-data folder, so they survive plugin updates.
class PresetManager
{
public:
    // One named bundle of parameter values, in real (un-normalised) units.
    // Parameters a preset doesn't name stay at their defaults.
    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    // Everything that differs between products. `folder` names the
    // application-data subdirectory, so two plugins never share a preset
    // directory; `treeTag` is the root element of a saved user preset.
    struct Product
    {
        juce::String folder;    // e.g. "Didge"
        juce::String treeTag;   // e.g. "DidgePreset"
        juce::String version;   // stamped into saved presets
    };

    PresetManager (juce::AudioProcessorValueTreeState& state,
                   Product product,
                   std::vector<Preset> factoryBank);

    // Called right AFTER a preset's values have been written into APVTS.
    // Lets the processor clear its "dirty since last load" tracking.
    void setPostLoadHook (std::function<void()> fn) { postLoadHook = std::move (fn); }

    // Product-specific state that belongs INSIDE a user preset alongside the
    // parameters -- the workshop benches, in Epi's case. The provider
    // returns a subtree stored verbatim in the saved file; the applier is
    // called on load with that subtree, AFTER the parameters and the post
    // load hook, so what the player saved wins over anything a name-matched
    // factory table would have painted. Presets saved before the provider
    // existed have no such child and the applier is simply not called.
    void setExtraState (juce::Identifier tag,
                        std::function<juce::ValueTree()> provider,
                        std::function<void (const juce::ValueTree&)> applier)
    {
        extraTag = tag;
        extraProvider = std::move (provider);
        extraApplier = std::move (applier);
    }

    juce::StringArray getFactoryNames() const;
    juce::StringArray getUserNames() const;

    void loadFactory (int index);
    void loadByName (const juce::String& name);   // factory or user
    void saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    void next();
    void previous();

    juce::String getCurrentName() const { return currentName; }

    // Restoring session state: the name is stored with the state, since it is
    // not a parameter and would otherwise be lost on reload.
    void setCurrentName (const juce::String& name) { currentName = name; }

    juce::File userPresetDirectory() const;

    // A preset name is typed by the user and then used as a filename. Path
    // separators, colons and the like either fail to write or escape the
    // preset directory entirely, so they are folded to underscores before the
    // name ever reaches the filesystem. The displayed name is unaffected --
    // only the file it lands in.
    static juce::String toFileName (const juce::String& presetName);

private:
    void applyPreset (const Preset& preset);
    juce::StringArray allNames() const;

    juce::AudioProcessorValueTreeState& apvts;
    Product product;
    std::vector<Preset> factory;
    juce::String currentName;
    std::function<void()> postLoadHook;
    juce::Identifier extraTag { "Extras" };
    std::function<juce::ValueTree()> extraProvider;
    std::function<void (const juce::ValueTree&)> extraApplier;
};

} // namespace epicommon
