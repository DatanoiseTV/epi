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

#include "common/PresetManager.h"

#include <vector>

namespace epi
{
    // The shipped bank. Values are in real parameter units; anything a preset
    // does not name resets to its default first, so presets are self-contained.
    std::vector<epicommon::PresetManager::Preset> makeFactoryPresets();

    // A factory preset may re-cut the harp itself: 88 pairs of
    // { length trim, gauge trim } for the tine workshop. Returns null for
    // presets that leave the player's own workshop alone -- which is the
    // deliberate default, so browsing amp voicings never destroys a scale
    // someone has painted.
    const std::array<std::array<float, 2>, 88>* factoryTineMods (const juce::String& name);

    // And the cabinet bench, for presets whose instrument lives on its own
    // speakers. Null for every preset that should leave the player's box.
    const std::array<float, 5>* factoryCabMods (const juce::String& name);
}
