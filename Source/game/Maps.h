#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace game
{

// A selectable level baked into the app. `json`/`size` point at the BinaryData
// resource; `name` is what the title screen shows.
struct MapInfo
{
    juce::String name;
    const char*  json = nullptr;
    int          size = 0;
};

// The maps available to pick from, in display order. Index 0 is the default.
const std::vector<MapInfo>& getMaps();

} // namespace game
