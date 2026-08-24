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

#include "FactoryPresets.h"
#include "PresetData.h"

#include <cstring>

namespace epi
{

// The bank itself -- values, comments, workshop tables -- lives in
// PresetData.h, framework-free, where tests/test_epi_presets.cpp renders
// every preset through the real engine and holds it to level, spectrum and
// character bounds. This file only folds that data into the PresetManager's
// shape: each preset is completed over the parameter defaults, so what ships
// is a full, self-contained voicing exactly as the previous hand-written
// bank was.
std::vector<epicommon::PresetManager::Preset> makeFactoryPresets()
{
    namespace pd = epi::presetdata;

    std::vector<epicommon::PresetManager::Preset> bank;
    bank.reserve (pd::kNumPresets);

    for (const auto& src : pd::kPresets)
    {
        epicommon::PresetManager::Preset p;
        p.name = src.name;
        p.values.reserve (std::size (pd::kDefaults));

        // Defaults first, then the preset's own departures over them. The
        // instrument index is authoritative in the data's own field, so a
        // preset cannot claim one instrument and sound as another.
        for (const auto& d : pd::kDefaults)
        {
            float v = d.value;
            if (std::strcmp (d.id, "instrument") == 0)
                v = static_cast<float> (src.instrument);
            for (std::size_t i = 0; i < src.numValues; ++i)
                if (std::strcmp (src.values[i].id, d.id) == 0)
                    { v = src.values[i].value; break; }
            p.values.emplace_back (juce::String (d.id), v);
        }
        bank.push_back (std::move (p));
    }
    return bank;
}

const std::array<std::array<float, 2>, 88>* factoryTineMods (const juce::String& name)
{
    return presetdata::tineModsFor (name.toRawUTF8());
}

const std::array<float, 5>* factoryCabMods (const juce::String& name)
{
    return presetdata::cabModsFor (name.toRawUTF8());
}

const std::array<float, 5>* factoryMicMods (const juce::String& name)
{
    return presetdata::micModsFor (name.toRawUTF8());
}

const std::array<float, 31>* factoryMicStage (const juce::String& name)
{
    return presetdata::micStageFor (name.toRawUTF8());
}

} // namespace epi
