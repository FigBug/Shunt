#pragma once

#include <JuceHeader.h>
#include <vector>
#include <optional>

namespace game
{

struct TrackNode
{
    juce::Point<float> position;
};

struct TrackSegment
{
    int nodeA = 0, nodeB = 0;
    float length = 0.0f;
};

struct SwitchInfo
{
    int node = 0;
    int stemSegment = 0;
    int normalSegment = 0;
    int reverseSegment = 0;
    bool reversed = false;
    float cooldown = 0.0f;
};

struct TrackPos
{
    int segment = 0;
    float distance = 0.0f;
};

struct SidingInfo
{
    int playerSlot = 0;
    int segment = 0;
    int bufferNode = 0;
    int switchNode = 0;
};

class TrackGraph
{
public:
    int addNode (juce::Point<float> pos);
    int addSegment (int a, int b);
    void addSwitch (int node, int stem, int normal, int reverse);
    void addSiding (int slot, int segment, int bufferNode, int switchNode);

    int numNodes() const noexcept    { return (int) nodes.size(); }
    int numSegments() const noexcept { return (int) segments.size(); }

    const TrackNode&    getNode (int i) const    { return nodes[(size_t) i]; }
    const TrackSegment& getSegment (int i) const { return segments[(size_t) i]; }

    const std::vector<SwitchInfo>& getSwitches() const { return switches; }
    std::vector<SwitchInfo>&       getSwitches()       { return switches; }
    const std::vector<SidingInfo>& getSidings() const  { return sidings; }

    SwitchInfo*       findSwitch (int node);
    const SwitchInfo* findSwitch (int node) const;

    const SidingInfo* findSiding (int slot) const;

    juce::Point<float> worldPos (TrackPos pos) const;
    float trackAngle (TrackPos pos, int dir) const;

    struct MoveResult
    {
        TrackPos pos;
        int dir = 1;
        bool stopped = false;
    };

    MoveResult advance (TrackPos pos, int dir, float dist) const;

    int nodeAtEnd (int segment, int whichEnd) const;

    std::optional<int> nextSwitchAhead (TrackPos pos, int dir) const;

    bool isMainLine (int segment) const;

private:
    std::vector<TrackNode>    nodes;
    std::vector<TrackSegment> segments;
    std::vector<SwitchInfo>   switches;
    std::vector<SidingInfo>   sidings;
    int mainLineEnd = -1;

    int routeThrough (int fromSeg, int atNode) const;

    friend void buildDefaultYard (TrackGraph& graph);
};

void buildDefaultYard (TrackGraph& graph);

} // namespace game
