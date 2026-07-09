#pragma once

#include <juce_core/juce_core.h>
#include "MapsDir.h"
#include <vector>

namespace game
{

// A selectable level, backed by a .json file in the shared maps folder.
struct MapInfo
{
    juce::String name;   // shown on the title screen (file name without extension)
    juce::File   file;   // the .json on disk
};

// The maps found in the shared folder, in display order. Scanned on first call
// (seeding the folder from the built-in maps if it is empty) and cached; call
// refreshMaps() to rescan. Index 0 is the default.
const std::vector<MapInfo>& getMaps();
void refreshMaps();

} // namespace game
