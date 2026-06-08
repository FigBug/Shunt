#include "Track.h"
#include <cmath>

namespace game
{

int TrackGraph::addNode (juce::Point<float> pos)
{
    nodes.push_back ({ pos });
    return (int) nodes.size() - 1;
}

int TrackGraph::addSegment (int a, int b)
{
    float len = nodes[(size_t) a].position.getDistanceFrom (nodes[(size_t) b].position);
    segments.push_back ({ a, b, len });
    return (int) segments.size() - 1;
}

void TrackGraph::addSwitch (int node, int stem, int normal, int reverse)
{
    switches.push_back ({ node, stem, normal, reverse, false });
}

void TrackGraph::addSiding (int slot, int segment, int bufferNode, int switchNode)
{
    sidings.push_back ({ slot, segment, bufferNode, switchNode });
}

SwitchInfo* TrackGraph::findSwitch (int node)
{
    for (auto& sw : switches)
        if (sw.node == node)
            return &sw;
    return nullptr;
}

const SwitchInfo* TrackGraph::findSwitch (int node) const
{
    for (const auto& sw : switches)
        if (sw.node == node)
            return &sw;
    return nullptr;
}

const SidingInfo* TrackGraph::findSiding (int slot) const
{
    for (const auto& s : sidings)
        if (s.playerSlot == slot)
            return &s;
    return nullptr;
}

juce::Point<float> TrackGraph::worldPos (TrackPos pos) const
{
    const auto& seg = segments[(size_t) pos.segment];
    auto a = nodes[(size_t) seg.nodeA].position;
    auto b = nodes[(size_t) seg.nodeB].position;
    float t = seg.length > 0.0f ? pos.distance / seg.length : 0.0f;
    return a + (b - a) * t;
}

float TrackGraph::trackAngle (TrackPos pos, int dir) const
{
    const auto& seg = segments[(size_t) pos.segment];
    auto a = nodes[(size_t) seg.nodeA].position;
    auto b = nodes[(size_t) seg.nodeB].position;
    auto d = b - a;
    float angle = std::atan2 (d.y, d.x);
    if (dir < 0)
        angle += juce::MathConstants<float>::pi;
    return angle;
}

int TrackGraph::nodeAtEnd (int segment, int whichEnd) const
{
    const auto& seg = segments[(size_t) segment];
    return whichEnd > 0 ? seg.nodeB : seg.nodeA;
}

int TrackGraph::routeThrough (int fromSeg, int atNode) const
{
    if (const auto* sw = findSwitch (atNode))
    {
        // Normal: stem ↔ normal (main through), reverse disconnected
        // Reversed: normal ↔ reverse (siding connected), stem disconnected
        if (fromSeg == sw->stemSegment)
            return sw->reversed ? -1 : sw->normalSegment;
        if (fromSeg == sw->normalSegment)
            return sw->reversed ? sw->reverseSegment : sw->stemSegment;
        if (fromSeg == sw->reverseSegment)
            return sw->reversed ? sw->normalSegment : -1;
        return -1;
    }

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        if (i == fromSeg) continue;
        const auto& s = segments[(size_t) i];
        if (s.nodeA == atNode || s.nodeB == atNode)
            return i;
    }
    return -1;
}

TrackGraph::MoveResult TrackGraph::advance (TrackPos pos, int dir, float dist) const
{
    if (dist <= 0.0f)
        return { pos, dir, false };

    const auto& seg = segments[(size_t) pos.segment];
    float newDist = pos.distance + (float) dir * dist;

    if (newDist >= 0.0f && newDist <= seg.length)
        return { { pos.segment, newDist }, dir, false };

    if (newDist > seg.length)
    {
        float overflow = newDist - seg.length;
        int crossedNode = seg.nodeB;
        int nextSeg = routeThrough (pos.segment, crossedNode);

        if (nextSeg < 0)
            return { { pos.segment, seg.length }, dir, true };

        const auto& ns = segments[(size_t) nextSeg];
        int newDir = (ns.nodeA == crossedNode) ? 1 : -1;
        float startDist = (newDir > 0) ? 0.0f : ns.length;

        return advance ({ nextSeg, startDist }, newDir, overflow);
    }

    float overflow = -newDist;
    int crossedNode = seg.nodeA;
    int nextSeg = routeThrough (pos.segment, crossedNode);

    if (nextSeg < 0)
        return { { pos.segment, 0.0f }, dir, true };

    const auto& ns = segments[(size_t) nextSeg];
    int newDir = (ns.nodeA == crossedNode) ? 1 : -1;
    float startDist = (newDir > 0) ? 0.0f : ns.length;

    return advance ({ nextSeg, startDist }, newDir, overflow);
}

std::optional<int> TrackGraph::nextSwitchAhead (TrackPos pos, int dir) const
{
    int seg = pos.segment;
    int curDir = dir;

    for (int steps = 0; steps < 20; ++steps)
    {
        const auto& s = segments[(size_t) seg];
        int nodeAhead = (curDir > 0) ? s.nodeB : s.nodeA;

        if (findSwitch (nodeAhead) != nullptr)
            return nodeAhead;

        int nextSeg = routeThrough (seg, nodeAhead);
        if (nextSeg < 0)
            return std::nullopt;

        const auto& ns = segments[(size_t) nextSeg];
        curDir = (ns.nodeA == nodeAhead) ? 1 : -1;
        seg = nextSeg;
    }

    return std::nullopt;
}

bool TrackGraph::isMainLine (int segment) const
{
    for (const auto& s : sidings)
        if (s.segment == segment)
            return false;
    return true;
}

void buildDefaultYard (TrackGraph& graph)
{
    //  P1(-16,-6)              P3(8,-6)
    //       \                    /
    // (-20,0)--S1(-10,0)--S2(-2,0)--S3(2,0)--S4(10,0)--(20,0)
    //                  \                          \
    //             P2(-8,6)                    P4(16,6)

    int n0  = graph.addNode ({ -20.0f,  0.0f });  // left buffer
    int n1  = graph.addNode ({ -10.0f,  0.0f });  // S1
    int n2  = graph.addNode ({  -2.0f,  0.0f });  // S2
    int n3  = graph.addNode ({   2.0f,  0.0f });  // S3
    int n4  = graph.addNode ({  10.0f,  0.0f });  // S4
    int n5  = graph.addNode ({  20.0f,  0.0f });  // right buffer
    int n6  = graph.addNode ({ -16.0f, -6.0f });  // P1 buffer
    int n7  = graph.addNode ({  -8.0f,  6.0f });  // P2 buffer
    int n8  = graph.addNode ({   8.0f, -6.0f });  // P3 buffer
    int n9  = graph.addNode ({  16.0f,  6.0f });  // P4 buffer

    int s0 = graph.addSegment (n0, n1);  // left buffer to S1
    int s1 = graph.addSegment (n1, n2);  // S1 to S2 (main)
    int s2 = graph.addSegment (n2, n3);  // S2 to S3 (main)
    int s3 = graph.addSegment (n3, n4);  // S3 to S4 (main)
    int s4 = graph.addSegment (n4, n5);  // S4 to right buffer
    int s5 = graph.addSegment (n1, n6);  // S1 to P1 buffer
    int s6 = graph.addSegment (n2, n7);  // S2 to P2 buffer
    int s7 = graph.addSegment (n3, n8);  // S3 to P3 buffer
    int s8 = graph.addSegment (n4, n9);  // S4 to P4 buffer

    // Normal: stem ↔ normal (main through), siding disconnected
    // Reversed: normal ↔ reverse (siding to centre-ward main), stem disconnected
    graph.addSwitch (n1, s0, s1, s5);  // S1: stem=left-buf, normal=right-main, reverse=P1
    graph.addSwitch (n2, s1, s2, s6);  // S2: stem=left-main, normal=right-main, reverse=P2
    graph.addSwitch (n3, s3, s2, s7);  // S3: stem=right-main, normal=left-main, reverse=P3
    graph.addSwitch (n4, s4, s3, s8);  // S4: stem=right-buf, normal=left-main, reverse=P4

    graph.addSiding (0, s5, n6, n1);
    graph.addSiding (1, s6, n7, n2);
    graph.addSiding (2, s7, n8, n3);
    graph.addSiding (3, s8, n9, n4);

    graph.mainLineEnd = s4;
}

} // namespace game
