#pragma once

#include <JuceHeader.h>
#include <vector>

// ============================================================================
// LevelDocument — the editable model for a Shunt level.
//
// It maps onto the JSON schema the game consumes (see Source/game/Track.cpp
// TrackGraph::loadFromJson):
//
//   nodes:     { id, x, y, kind, degree }
//   edges:     { id, from, to, polyline:[[x,y]...], length, buffer_end }
//   drop_offs: { colour, node }
//
// Switches / crossings are inferred by the game loader from node degree and
// edge tangents, so the editor only authors geometry + connectivity + the
// drop-offs. Coordinates are stored in raw world units (the game applies its
// own 0.04 scale at load time).
//
// The editor additionally writes a "spawns" array (car start markers) which is
// an authoring extension the game can adopt later.
// ============================================================================

struct EdNode
{
    int id = 0;
    juce::Point<float> pos;
};

struct EdSegment
{
    int id = 0;
    int from = 0;
    int to = 0;
    bool curved = false;
    juce::Point<float> control;   // quadratic-bezier control point (world), used when curved
};

struct EdDropOff
{
    int colour = 0;               // 0..3 -> the game's four slot colours
    int node = 0;
};

struct EdSpawn
{
    juce::Point<float> pos;
    int colour = -1;              // -1 = any / random
};

class LevelDocument
{
public:
    std::vector<EdNode>     nodes;
    std::vector<EdSegment>  segments;
    std::vector<EdDropOff>  dropOffs;
    std::vector<EdSpawn>    spawns;
    int                     totalCars = 20;   // spread randomly across the spawns

    // ---- mutation ----------------------------------------------------------
    int  addNode (juce::Point<float> p);
    int  addSegment (int from, int to, bool curved);

    // Emit a closed rounded-rectangle loop: 4 straight sides + 4 quarter-arc
    // corners (8 nodes, 8 segments). The whole thing is degree-2, which the
    // game engine traverses continuously (verified — see the loop test).
    void addRoundedRectLoop (juce::Rectangle<float> bounds, float radius);

    void removeNode (int id);       // also removes attached segments + drop-offs
    void removeSegment (int id);
    // Fold `fromId` into `intoId`: reattach its segments, drop the resulting
    // self-loops / duplicate segments, move its drop-off, then delete it.
    void mergeNode (int fromId, int intoId);

    // Split a segment at parameter t (0..1 along it), producing a node in the
    // middle so a third track can connect there (i.e. form a switch). Straight
    // segments split into two straights; curves split exactly (de Casteljau).
    // If useNodeId >= 0 that existing node becomes the split point (and is moved
    // onto the track); otherwise a new node is created. Returns the mid node id.
    int splitSegment (int segId, float t, int useNodeId = -1);

    // Repair loose ends: for every node with <= 1 connection sitting within
    // `tolerance` of another segment, split that segment onto it. Returns the
    // number of welds made. Fixes tracks/drop-offs that look joined but aren't.
    int weldLooseEnds (float tolerance);

    void clear();

    // ---- lookup ------------------------------------------------------------
    EdNode*       findNode (int id);
    const EdNode* findNode (int id) const;
    EdSegment*    findSegment (int id);
    int           nodeDegree (int id) const;
    const EdDropOff* dropOffForNode (int nodeId) const;
    void          setDropOff (int nodeId, int colour);   // assign / update
    void          clearDropOff (int nodeId);

    // ---- geometry ----------------------------------------------------------
    juce::Point<float> defaultControlFor (int fromNode, int toNode) const;
    std::vector<juce::Point<float>> polylineFor (const EdSegment&) const;
    float lengthFor (const EdSegment&) const;

    // ---- serialisation -----------------------------------------------------
    juce::String toJsonString() const;
    bool loadFromString (const juce::String& json);

    static juce::Colour slotColour (int index);

private:
    int nextNodeId = 0;
    int nextSegmentId = 0;
};
