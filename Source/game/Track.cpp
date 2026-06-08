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

void TrackGraph::addDropOff (int colourIndex, int node)
{
    dropOffs.push_back ({ colourIndex, node });
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
    for (const auto& sw : switches)
        if (segment == sw.reverseSegment)
            return false;
    return true;
}

std::vector<int> TrackGraph::mainLineSegments() const
{
    std::vector<int> result;
    for (int i = 0; i < (int) segments.size(); ++i)
        if (isMainLine (i))
            result.push_back (i);
    return result;
}

void buildDefaultYard (TrackGraph& graph)
{
    // Large Jinryō-style rail yard:
    // 2 main through lines, throat junction, 10 sidings in 2 groups of 5.
    // Upper siding 1 and lower siding 1 are through-tracks reconnecting
    // to the main lines on the right. The rest are dead-end holding/player sidings.
    // Players: P1=upper 2, P2=upper 4, P3=lower 2, P4=lower 4.

    // ── Main line 1 (y=0) ──
    int nML1 = graph.addNode ({  -5.0f,  0.0f });
    int nSA  = graph.addNode ({  20.0f,  0.0f });
    int nRA  = graph.addNode ({  65.0f,  0.0f });
    int nMR1 = graph.addNode ({  80.0f,  0.0f });

    // ── Main line 2 (y=3) ──
    int nML2 = graph.addNode ({  -5.0f,  3.0f });
    int nSB  = graph.addNode ({  18.0f,  3.0f });
    int nRB  = graph.addNode ({  65.0f,  3.0f });
    int nMR2 = graph.addNode ({  80.0f,  3.0f });

    // ── Throat junction ──
    int nJn  = graph.addNode ({  22.0f,  5.0f });

    // ── Upper lead switches (y 7–15, stepping +2) ──
    int nU1  = graph.addNode ({  24.0f,  7.0f });
    int nU2  = graph.addNode ({  26.0f,  9.0f });
    int nU3  = graph.addNode ({  28.0f, 11.0f });
    int nU4  = graph.addNode ({  30.0f, 13.0f });
    int nU5  = graph.addNode ({  32.0f, 15.0f });

    // ── Lower lead switches (y 18–26, stepping +2) ──
    int nL1  = graph.addNode ({  34.0f, 18.0f });
    int nL2  = graph.addNode ({  36.0f, 20.0f });
    int nL3  = graph.addNode ({  38.0f, 22.0f });
    int nL4  = graph.addNode ({  40.0f, 24.0f });
    int nL5  = graph.addNode ({  42.0f, 26.0f });
    int nBot = graph.addNode ({  44.0f, 28.0f });

    // ── Upper siding ends ──
    int nUT  = graph.addNode ({  63.0f,  7.0f });  // through-track end (reconnects)
    int nUB2 = graph.addNode ({  75.0f,  9.0f });  // P1 buffer
    int nUB3 = graph.addNode ({  75.0f, 11.0f });  // holding
    int nUB4 = graph.addNode ({  75.0f, 13.0f });  // P2 buffer
    int nUB5 = graph.addNode ({  75.0f, 15.0f });  // holding

    // ── Lower siding ends ──
    int nLT  = graph.addNode ({  63.0f, 18.0f });  // through-track end (reconnects)
    int nLB2 = graph.addNode ({  75.0f, 20.0f });  // P3 buffer
    int nLB3 = graph.addNode ({  75.0f, 22.0f });  // holding
    int nLB4 = graph.addNode ({  75.0f, 24.0f });  // P4 buffer
    int nLB5 = graph.addNode ({  75.0f, 26.0f });  // holding

    // ════════════ Segments ════════════

    // Main line 1
    int smL1  = graph.addSegment (nML1, nSA);
    int smM1  = graph.addSegment (nSA,  nRA);
    int smR1  = graph.addSegment (nRA,  nMR1);

    // Main line 2
    int smL2  = graph.addSegment (nML2, nSB);
    int smM2  = graph.addSegment (nSB,  nRB);
    int smR2  = graph.addSegment (nRB,  nMR2);

    // Throat diagonals
    int sdA   = graph.addSegment (nSA, nJn);   // SA → Jn
    int sdB   = graph.addSegment (nSB, nJn);   // SB → Jn

    // Lead: Jn → upper switches → lower switches → bottom buf
    int sJ1   = graph.addSegment (nJn, nU1);
    int sU12  = graph.addSegment (nU1, nU2);
    int sU23  = graph.addSegment (nU2, nU3);
    int sU34  = graph.addSegment (nU3, nU4);
    int sU45  = graph.addSegment (nU4, nU5);
    int sUL   = graph.addSegment (nU5, nL1);   // upper→lower gap
    int sL12  = graph.addSegment (nL1, nL2);
    int sL23  = graph.addSegment (nL2, nL3);
    int sL34  = graph.addSegment (nL3, nL4);
    int sL45  = graph.addSegment (nL4, nL5);
    int sLB   = graph.addSegment (nL5, nBot);

    // Upper sidings (horizontal)
    int su1   = graph.addSegment (nU1, nUT);    // through track
    int su2   = graph.addSegment (nU2, nUB2);   // P1
    int su3   = graph.addSegment (nU3, nUB3);   // holding
    int su4   = graph.addSegment (nU4, nUB4);   // P2
    int su5   = graph.addSegment (nU5, nUB5);   // holding

    // Lower sidings (horizontal)
    int sl1   = graph.addSegment (nL1, nLT);    // through track
    int sl2   = graph.addSegment (nL2, nLB2);   // P3
    int sl3   = graph.addSegment (nL3, nLB3);   // holding
    int sl4   = graph.addSegment (nL4, nLB4);   // P4
    int sl5   = graph.addSegment (nL5, nLB5);   // holding

    // Through-track reconnections (diagonal back to main)
    int stU   = graph.addSegment (nUT, nRA);    // upper through → main1
    int stL   = graph.addSegment (nLT, nRB);    // lower through → main2

    // ════════════ Switches ════════════
    // Normal: stem↔normal, Reversed: normal↔reverse

    // SA: main1 left junction
    graph.addSwitch (nSA, smL1, smM1, sdA);

    // SB: main2 left junction
    graph.addSwitch (nSB, smL2, smM2, sdB);

    // Jn: throat — normal = SA feed↔lead, reversed = SB feed↔lead
    graph.addSwitch (nJn, sdB, sdA, sJ1);

    // Upper siding switches — normal = lead through, reversed = lead↔siding
    graph.addSwitch (nU1, sU12, sJ1,  su1);
    graph.addSwitch (nU2, sU23, sU12, su2);
    graph.addSwitch (nU3, sU34, sU23, su3);
    graph.addSwitch (nU4, sU45, sU34, su4);
    graph.addSwitch (nU5, sUL,  sU45, su5);

    // Lower siding switches
    graph.addSwitch (nL1, sL12, sUL,  sl1);
    graph.addSwitch (nL2, sL23, sL12, sl2);
    graph.addSwitch (nL3, sL34, sL23, sl3);
    graph.addSwitch (nL4, sL45, sL34, sl4);
    graph.addSwitch (nL5, sLB,  sL45, sl5);

    // RA: main1 right — through track reconnects
    graph.addSwitch (nRA, smR1, smM1, stU);

    // RB: main2 right — through track reconnects
    graph.addSwitch (nRB, smR2, smM2, stL);

    // ════════════ Player sidings ════════════
    graph.addSiding (0, su2, nUB2, nU2);   // P1 = upper siding 2
    graph.addSiding (1, su4, nUB4, nU4);   // P2 = upper siding 4
    graph.addSiding (2, sl2, nLB2, nL2);   // P3 = lower siding 2
    graph.addSiding (3, sl4, nLB4, nL4);   // P4 = lower siding 4

    // ════════════ Drop-off zones (4 colours at 4 main line endpoints) ════════════
    graph.addDropOff (0, nML1);   // red    — main1 left
    graph.addDropOff (1, nMR1);   // blue   — main1 right
    graph.addDropOff (2, nML2);   // green  — main2 left
    graph.addDropOff (3, nMR2);   // yellow — main2 right
}

} // namespace game
