/*
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#include "PresetManager.h"

namespace epicommon
{

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state,
                              Product prod,
                              std::vector<Preset> factoryBank)
    : apvts (state), product (std::move (prod)), factory (std::move (factoryBank))
{
    currentName = factory.empty() ? juce::String() : factory.front().name;
}

juce::String PresetManager::toFileName (const juce::String& presetName)
{
    juce::String out;
    out.preallocateBytes (static_cast<size_t> (presetName.getNumBytesAsUTF8()) + 1);

    for (auto c : presetName)
    {
        // Anything that is a path separator, a Windows-reserved character or a
        // control character becomes an underscore. Everything else -- including
        // spaces and non-ASCII letters -- is kept, so names stay readable.
        const bool reserved = c == '/' || c == '\\' || c == ':' || c == '*'
                           || c == '?' || c == '"'  || c == '<' || c == '>'
                           || c == '|' || c < 0x20;
        // Both arms must already be juce_wchar: a bare '_' is a char, the
        // ternary then promotes the pair to int, and String::operator+= (int)
        // appends the number rather than the character.
        const juce::juce_wchar replacement = reserved ? static_cast<juce::juce_wchar> ('_') : c;
        out += replacement;
    }

    // A leading dot hides the file on Unix and a trailing dot or space is
    // silently stripped by Windows, which would make the name unaddressable.
    out = out.trim();
    while (out.startsWithChar ('.')) out = out.substring (1).trim();
    while (out.endsWithChar   ('.')) out = out.dropLastCharacters (1).trim();

    return out.isEmpty() ? juce::String ("Untitled") : out;
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;
    for (const auto& p : factory) names.add (p.name);
    return names;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;
    const auto dir = userPresetDirectory();
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::File PresetManager::userPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (product.folder).getChildFile ("Presets");
}

void PresetManager::applyPreset (const Preset& preset)
{
    // Reset everything to defaults first so presets are self-contained.
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }

    for (const auto& [id, value] : preset.values)
        if (auto* rp = apvts.getParameter (id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
            rp->endChangeGesture();
        }

    currentName = preset.name;
    if (postLoadHook) postLoadHook();
}

void PresetManager::loadFactory (int index)
{
    if (index >= 0 && index < static_cast<int> (factory.size()))
        applyPreset (factory[static_cast<size_t> (index)]);
}

void PresetManager::loadByName (const juce::String& name)
{
    for (size_t i = 0; i < factory.size(); ++i)
        if (factory[i].name == name)
        {
            applyPreset (factory[i]);
            return;
        }

    const auto file = userPresetDirectory().getChildFile (toFileName (name) + ".xml");
    if (! file.existsAsFile()) return;

    if (auto xml = juce::parseXML (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
        {
            Preset p;
            p.name = name;
            // Stored as the APVTS tree; convert to id/value pairs so the
            // same defaults-first apply path runs for user presets too.
            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                auto child = tree.getChild (i);
                if (child.hasProperty ("id") && child.hasProperty ("value"))
                    p.values.emplace_back (child["id"].toString(), (float) child["value"]);
            }
            applyPreset (p);
        }
    }
}

void PresetManager::saveUser (const juce::String& name)
{
    auto dir = userPresetDirectory();
    dir.createDirectory();

    juce::ValueTree tree (product.treeTag);
    tree.setProperty ("version", product.version, nullptr);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            juce::ValueTree param ("Param");
            param.setProperty ("id", rp->getParameterID(), nullptr);
            param.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
            tree.addChild (param, -1, nullptr);
        }

    const auto safe = toFileName (name);
    if (auto xml = tree.createXml())
        xml->writeTo (dir.getChildFile (safe + ".xml"));

    // Show the name the file actually got, not the one that was typed: the
    // preset list is built from filenames, so anything else leaves the header
    // naming a preset that does not appear in the browser.
    currentName = safe;
    if (postLoadHook) postLoadHook();
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return userPresetDirectory().getChildFile (toFileName (name) + ".xml").deleteFile();
}

juce::StringArray PresetManager::allNames() const
{
    auto names = getFactoryNames();
    names.addArray (getUserNames());
    return names;
}

void PresetManager::next()
{
    const auto names = allNames();
    if (names.isEmpty()) return;
    const int idx = names.indexOf (currentName);
    loadByName (names[(idx + 1 + names.size()) % names.size()]);
}

void PresetManager::previous()
{
    const auto names = allNames();
    if (names.isEmpty()) return;
    const int idx = juce::jmax (0, names.indexOf (currentName));
    loadByName (names[(idx - 1 + names.size()) % names.size()]);
}

} // namespace epicommon
