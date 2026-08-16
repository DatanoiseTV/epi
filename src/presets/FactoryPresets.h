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

#pragma once

#include "../common/PresetManager.h"

#include <vector>

namespace didge
{
    // The shipped preset bank. Values are in real parameter units; anything a
    // preset does not name resets to its default first, so presets are
    // self-contained.
    std::vector<epicommon::PresetManager::Preset> makeFactoryPresets();
}
