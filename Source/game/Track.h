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
    std::vector<juce::Point<float>> polyline;
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

struct CrossingInfo
{
    int node = 0;
    int pairA1 = 0, pairA2 = 0;
    int pairB1 = 0, pairB2 = 0;
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

// Authored car start position (from the level editor's "spawns" array). Stored
// in world coordinates; resolved to a TrackPos at placement time.
struct SpawnPoint
{
    juce::Point<float> pos;
    int colour = -1;   // -1 = let the game assign a colour, else a CarColour index
};

class TrackGraph
{
public:
    int addNode (juce::Point<float> pos);
    int addSegment (int a, int b);
    int addSegmentWithPolyline (int a, int b, std::vector<juce::Point<float>> poly, float len);
    void addSwitch (int node, int stem, int normal, int reverse);
    void addCrossing (int node, int a1, int a2, int b1, int b2);
    void addSiding (int slot, int segment, int bufferNode, int switchNode);

    struct DropOffZone { int colourIndex = 0; int node = 0; };
    void addDropOff (int colourIndex, int node);
    const std::vector<DropOffZone>& getDropOffs() const { return dropOffs; }

    void addSpawn (juce::Point<float> pos, int colour);
    const std::vector<SpawnPoint>& getSpawns() const { return spawns; }

    // Total cars for the level, spread randomly across the spawn points. Falls
    // back to the spawn count when the level doesn't specify one.
    int getCarCount() const { return carCount; }

    // Snap a world point onto the nearest track segment, returning the closest
    // position on the graph. Used to resolve authored spawn points to TrackPos.
    TrackPos nearestTrackPos (juce::Point<float> world) const;

    int numNodes() const noexcept    { return (int) nodes.size(); }
    int numSegments() const noexcept { return (int) segments.size(); }

    const TrackNode&    getNode (int i) const    { return nodes[(size_t) i]; }
    const TrackSegment& getSegment (int i) const { return segments[(size_t) i]; }

    const std::vector<SwitchInfo>&   getSwitches()  const { return switches; }
    std::vector<SwitchInfo>&         getSwitches()        { return switches; }
    const std::vector<CrossingInfo>& getCrossings() const { return crossings; }
    const std::vector<SidingInfo>&   getSidings()   const { return sidings; }

    SwitchInfo*       findSwitch (int node);
    const SwitchInfo* findSwitch (int node) const;
    const SidingInfo* findSiding (int slot) const;

    juce::Point<float> worldPos (TrackPos pos) const;
    float trackAngle (TrackPos pos, int dir) const;

    struct MoveResult { TrackPos pos; int dir = 1; bool stopped = false; };
    MoveResult advance (TrackPos pos, int dir, float dist) const;

    int nodeAtEnd (int segment, int whichEnd) const;

    // The segment reached when leaving fromSeg through atNode, honouring
    // switch state and crossing pairs. -1 if the track ends there.
    int routeThrough (int fromSeg, int atNode) const;
    std::optional<int> nextSwitchAhead (TrackPos pos, int dir) const;

    bool isMainLine (int segment) const;
    std::vector<int> mainLineSegments() const;

    // BFS pathfinding ignoring switch states. Returns sequence of segments from
    // the segment containing startPos to the segment containing targetPos.
    // Empty if no path or already on the same segment.
    struct PathResult
    {
        std::vector<int> segments;
        int firstDir = 1; // direction to go on the first segment
    };
    PathResult findPath (TrackPos startPos, int startDir, TrackPos targetPos) const;

    void loadFromJson (const juce::String& json, float scale = 1.0f);

    std::vector<int> findBufferEdges() const;
    std::vector<int> findEndpointNodes() const;

private:
    std::vector<TrackNode>    nodes;
    std::vector<TrackSegment> segments;
    std::vector<SwitchInfo>   switches;
    std::vector<CrossingInfo> crossings;
    std::vector<SidingInfo>   sidings;
    std::vector<DropOffZone>  dropOffs;
    std::vector<SpawnPoint>   spawns;
    int carCount = 0;
    int mainLineEnd = -1;

    std::vector<int> segmentsAtNode (int node) const;

    friend void buildDefaultYard (TrackGraph& graph);
};

void buildDefaultYard (TrackGraph& graph);

} // namespace game
