#include "Maps.h"
#include "BinaryData.h"

namespace game
{

const std::vector<MapInfo>& getMaps()
{
    static const std::vector<MapInfo> maps = {
        { "Chicago",  BinaryData::Chicago_json,  BinaryData::Chicago_jsonSize },
        { "New York", BinaryData::New_York_json, BinaryData::New_York_jsonSize },
        { "Santa Fe", BinaryData::Santa_Fe_json, BinaryData::Santa_Fe_jsonSize },
    };
    return maps;
}

} // namespace game
