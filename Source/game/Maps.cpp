#include "Maps.h"
#include "BinaryData.h"

namespace game
{

const std::vector<MapInfo>& getMaps()
{
    static const std::vector<MapInfo> maps = {
        { "Mobile",   BinaryData::Mobile_json,   BinaryData::Mobile_jsonSize },
        { "Santa Fe", BinaryData::Santa_Fe_json, BinaryData::Santa_Fe_jsonSize },
    };
    return maps;
}

} // namespace game
