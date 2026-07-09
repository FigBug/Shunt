#include "Maps.h"

namespace game
{

namespace
{
    std::vector<MapInfo> gMaps;
    bool gScanned = false;
}

void refreshMaps()
{
    gMaps.clear();

    auto dir = mapsDirectory();

    // The installer populates the shared folder. For a portable/Linux build with
    // no installer, seed it once from a "Maps" folder shipped next to the binary.
    if (dir.findChildFiles (juce::File::findFiles, false, "*.json").isEmpty())
    {
        auto bundled = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                           .getParentDirectory().getChildFile ("Maps");
        if (bundled.isDirectory())
            for (const auto& f : bundled.findChildFiles (juce::File::findFiles, false, "*.json"))
                dir.getChildFile (f.getFileName()).replaceWithText (f.loadFileAsString());
    }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();   // stable alphabetical order for the picker

    for (const auto& f : files)
        gMaps.push_back ({ f.getFileNameWithoutExtension(), f });

    gScanned = true;
}

const std::vector<MapInfo>& getMaps()
{
    if (! gScanned)
        refreshMaps();
    return gMaps;
}

} // namespace game
