#include "Track.h"
#include <cmath>
#include <algorithm>
#include <queue>
#include <map>

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
    segments.push_back ({ a, b, len, {} });
    return (int) segments.size() - 1;
}

int TrackGraph::addSegmentWithPolyline (int a, int b,
                                        std::vector<juce::Point<float>> poly, float len)
{
    segments.push_back ({ a, b, len, std::move (poly) });
    return (int) segments.size() - 1;
}

void TrackGraph::addSwitch (int node, int stem, int normal, int reverse)
{
    switches.push_back ({ node, stem, normal, reverse, false, 0.0f });
}

void TrackGraph::addCrossing (int node, int a1, int a2, int b1, int b2)
{
    crossings.push_back ({ node, a1, a2, b1, b2 });
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
        if (sw.node == node) return &sw;
    return nullptr;
}

const SwitchInfo* TrackGraph::findSwitch (int node) const
{
    for (const auto& sw : switches)
        if (sw.node == node) return &sw;
    return nullptr;
}

const SidingInfo* TrackGraph::findSiding (int slot) const
{
    for (const auto& s : sidings)
        if (s.playerSlot == slot) return &s;
    return nullptr;
}

std::vector<int> TrackGraph::segmentsAtNode (int node) const
{
    std::vector<int> result;
    for (int i = 0; i < (int) segments.size(); ++i)
    {
        const auto& s = segments[(size_t) i];
        if (s.nodeA == node || s.nodeB == node)
            result.push_back (i);
    }
    return result;
}

juce::Point<float> TrackGraph::worldPos (TrackPos pos) const
{
    const auto& seg = segments[(size_t) pos.segment];

    if (seg.polyline.size() >= 2)
    {
        float remaining = pos.distance;
        for (size_t i = 0; i + 1 < seg.polyline.size(); ++i)
        {
            float segLen = seg.polyline[i].getDistanceFrom (seg.polyline[i + 1]);
            if (remaining <= segLen || i + 2 == seg.polyline.size())
            {
                float t = segLen > 0.0f ? juce::jmin (1.0f, remaining / segLen) : 0.0f;
                return seg.polyline[i] + (seg.polyline[i + 1] - seg.polyline[i]) * t;
            }
            remaining -= segLen;
        }
        return seg.polyline.back();
    }

    auto a = nodes[(size_t) seg.nodeA].position;
    auto b = nodes[(size_t) seg.nodeB].position;
    float t = seg.length > 0.0f ? pos.distance / seg.length : 0.0f;
    return a + (b - a) * t;
}

float TrackGraph::trackAngle (TrackPos pos, int dir) const
{
    const auto& seg = segments[(size_t) pos.segment];

    if (seg.polyline.size() >= 2)
    {
        float remaining = pos.distance;
        for (size_t i = 0; i + 1 < seg.polyline.size(); ++i)
        {
            float segLen = seg.polyline[i].getDistanceFrom (seg.polyline[i + 1]);
            if (remaining <= segLen || i + 2 == seg.polyline.size())
            {
                auto d = seg.polyline[i + 1] - seg.polyline[i];
                float angle = std::atan2 (d.y, d.x);
                if (dir < 0) angle += juce::MathConstants<float>::pi;
                return angle;
            }
            remaining -= segLen;
        }
    }

    auto a = nodes[(size_t) seg.nodeA].position;
    auto b = nodes[(size_t) seg.nodeB].position;
    auto d = b - a;
    float angle = std::atan2 (d.y, d.x);
    if (dir < 0) angle += juce::MathConstants<float>::pi;
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
        if (fromSeg == sw->stemSegment)
            return sw->reversed ? sw->reverseSegment : sw->normalSegment;
        if (fromSeg == sw->normalSegment)
            return sw->stemSegment;
        if (fromSeg == sw->reverseSegment)
            return sw->stemSegment;
        return -1;
    }

    for (const auto& cr : crossings)
    {
        if (cr.node != atNode) continue;
        if (fromSeg == cr.pairA1) return cr.pairA2;
        if (fromSeg == cr.pairA2) return cr.pairA1;
        if (fromSeg == cr.pairB1) return cr.pairB2;
        if (fromSeg == cr.pairB2) return cr.pairB1;
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

    for (int steps = 0; steps < 50; ++steps)
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
        if (s.segment == segment) return false;
    for (const auto& sw : switches)
        if (segment == sw.reverseSegment) return false;
    return true;
}

std::vector<int> TrackGraph::mainLineSegments() const
{
    std::vector<int> result;
    for (int i = 0; i < (int) segments.size(); ++i)
        if (isMainLine (i)) result.push_back (i);
    return result;
}

std::vector<int> TrackGraph::findBufferEdges() const
{
    std::vector<int> result;
    for (int i = 0; i < (int) segments.size(); ++i)
    {
        const auto& seg = segments[(size_t) i];
        for (int endpoint : { seg.nodeA, seg.nodeB })
        {
            int conns = 0;
            for (const auto& s : segments)
                if (s.nodeA == endpoint || s.nodeB == endpoint)
                    ++conns;
            if (conns == 1)
            {
                result.push_back (i);
                break;
            }
        }
    }
    return result;
}

std::vector<int> TrackGraph::findEndpointNodes() const
{
    std::vector<int> result;
    for (int n = 0; n < (int) nodes.size(); ++n)
    {
        int conns = 0;
        for (const auto& s : segments)
            if (s.nodeA == n || s.nodeB == n)
                ++conns;
        if (conns == 1)
        {
            bool isBuf = false;
            for (int si = 0; si < (int) segments.size(); ++si)
            {
                const auto& seg = segments[(size_t) si];
                if ((seg.nodeA == n || seg.nodeB == n) && seg.length < 20.0f)
                    isBuf = true;
            }
            if (! isBuf)
                result.push_back (n);
        }
    }
    return result;
}

// ════════════════════════════════════════════════════════════════
// JSON loader
// ════════════════════════════════════════════════════════════════

void TrackGraph::loadFromJson (const juce::String& json, float scale)
{
    auto root = juce::JSON::parse (json);
    if (! root.isObject()) return;

    auto nodesArray = *root.getProperty ("nodes", {}).getArray();
    auto edgesArray = *root.getProperty ("edges", {}).getArray();

    // Map from JSON node id → internal node index
    std::map<int, int> nodeMap;

    for (const auto& nv : nodesArray)
    {
        int jsonId = (int) nv.getProperty ("id", -1);
        float x = (float) (double) nv.getProperty ("x", 0.0) * scale;
        float y = (float) (double) nv.getProperty ("y", 0.0) * scale;
        int idx = addNode ({ x, y });
        nodeMap[jsonId] = idx;
    }

    // Map from JSON edge id → internal segment index
    std::map<int, int> edgeMap;

    for (const auto& ev : edgesArray)
    {
        int edgeId = (int) ev.getProperty ("id", -1);
        int fromId = (int) ev.getProperty ("from", -1);
        bool bufferEnd = (bool) ev.getProperty ("buffer_end", false);
        float length = (float) (double) ev.getProperty ("length", 0.0) * scale;

        int toId = -1;
        if (! ev.getProperty ("to", {}).isVoid())
            toId = (int) ev.getProperty ("to", -1);

        auto polyArr = *ev.getProperty ("polyline", {}).getArray();
        std::vector<juce::Point<float>> poly;
        for (const auto& pt : polyArr)
        {
            if (auto* arr = pt.getArray())
            {
                float px = (float) (double) (*arr)[0] * scale;
                float py = (float) (double) (*arr)[1] * scale;
                poly.push_back ({ px, py });
            }
        }

        int nA = nodeMap.count (fromId) ? nodeMap[fromId] : -1;
        int nB = -1;

        if (toId >= 0 && nodeMap.count (toId))
        {
            nB = nodeMap[toId];
        }
        else if (bufferEnd && poly.size() >= 2)
        {
            auto bufPos = poly.back();
            nB = addNode (bufPos);
        }
        else
        {
            continue;
        }

        if (nA < 0) continue;

        int segIdx = addSegmentWithPolyline (nA, nB, std::move (poly), length);
        edgeMap[edgeId] = segIdx;
    }

    // Build switch/crossing info from node degrees and edge tangent angles
    for (const auto& nv : nodesArray)
    {
        int jsonId = (int) nv.getProperty ("id", -1);
        juce::String kind = nv.getProperty ("kind", "").toString();
        int degree = (int) nv.getProperty ("degree", 0);

        if (kind != "switch" || degree < 3)
            continue;

        int nodeIdx = nodeMap[jsonId];
        auto incident = segmentsAtNode (nodeIdx);

        if ((int) incident.size() < 3)
            continue;

        auto leavingTangent = [&, scale] (int segIdx) -> juce::Point<float>
        {
            const auto& seg = segments[(size_t) segIdx];
            bool fromA = (seg.nodeA == nodeIdx);

            // Sample a point ~20% along the edge for a robust tangent
            float sampleDist = juce::jmin (seg.length * 0.2f, 3.0f * scale);
            auto origin = worldPos ({ segIdx, fromA ? 0.0f : seg.length });
            auto sample = worldPos ({ segIdx, fromA ? sampleDist : seg.length - sampleDist });
            auto d = sample - origin;

            float len = d.getDistanceFromOrigin();
            return len > 0.0f ? d / len : juce::Point<float>();
        };

        auto dotP = [] (juce::Point<float> a, juce::Point<float> b)
        { return a.x * b.x + a.y * b.y; };

        if (degree == 3 && (int) incident.size() == 3)
        {
            int e0 = incident[0], e1 = incident[1], e2 = incident[2];
            auto d0 = leavingTangent (e0);
            auto d1 = leavingTangent (e1);
            auto d2 = leavingTangent (e2);

            float dot01 = dotP (d0, d1);
            float dot12 = dotP (d1, d2);
            float dot02 = dotP (d0, d2);

            // The two LEGS point in similar directions (highest dot).
            // The remaining edge is the STEM.
            int stem, leg1, leg2;
            if (dot01 >= dot12 && dot01 >= dot02)
                { leg1 = e0; leg2 = e1; stem = e2; }
            else if (dot12 >= dot01 && dot12 >= dot02)
                { leg1 = e1; leg2 = e2; stem = e0; }
            else
                { leg1 = e0; leg2 = e2; stem = e1; }

            // The "normal" leg forms the straighter path with the stem
            // (more negative dot = more opposite = straighter).
            auto dStem = leavingTangent (stem);
            auto dL1 = leavingTangent (leg1);
            auto dL2 = leavingTangent (leg2);
            int normal  = (dotP (dStem, dL1) < dotP (dStem, dL2)) ? leg1 : leg2;
            int reverse = (normal == leg1) ? leg2 : leg1;

            addSwitch (nodeIdx, stem, normal, reverse);
        }
        else if (degree >= 4 && (int) incident.size() >= 4)
        {
            // Find best pairing of 4 edges into 2 through-pairs
            int n = juce::jmin ((int) incident.size(), 4);
            std::vector<juce::Point<float>> tangents;
            for (int i = 0; i < n; ++i)
                tangents.push_back (leavingTangent (incident[(size_t) i]));

            // 3 possible pairings of 4 items
            int pairings[3][4] = { {0,1,2,3}, {0,2,1,3}, {0,3,1,2} };
            float bestScore = 1e9f;
            int bestP = 0;

            for (int p = 0; p < 3; ++p)
            {
                float score = dotP (tangents[(size_t) pairings[p][0]],
                                    tangents[(size_t) pairings[p][1]])
                            + dotP (tangents[(size_t) pairings[p][2]],
                                    tangents[(size_t) pairings[p][3]]);
                if (score < bestScore) { bestScore = score; bestP = p; }
            }

            addCrossing (nodeIdx,
                         incident[(size_t) pairings[bestP][0]],
                         incident[(size_t) pairings[bestP][1]],
                         incident[(size_t) pairings[bestP][2]],
                         incident[(size_t) pairings[bestP][3]]);
        }
    }

    // Load drop-off zones from JSON
    if (auto* dropArr = root.getProperty ("drop_offs", {}).getArray())
    {
        for (const auto& dv : *dropArr)
        {
            int colour = (int) dv.getProperty ("colour", 0);
            int nodeId = (int) dv.getProperty ("node", -1);
            if (nodeMap.count (nodeId))
                addDropOff (colour, nodeMap[nodeId]);
        }
    }
}

// ════════════════════════════════════════════════════════════════
// Hardcoded yard (fallback)
// ════════════════════════════════════════════════════════════════

TrackGraph::PathResult TrackGraph::findPath (TrackPos startPos, int startDir, TrackPos targetPos) const
{
    int startSeg = startPos.segment;
    int targetSeg = targetPos.segment;

    if (startSeg == targetSeg)
    {
        int dir = (targetPos.distance > startPos.distance) ? 1 : -1;
        return { { startSeg }, dir };
    }

    // BFS on segments, respecting switch topology:
    // At a switch: leg→stem always, stem→either leg. Never leg→leg directly.
    // At a crossing: paired routes only.
    // At a plain node: any to any.

    auto canTraverse = [&] (int fromSeg, int node, int toSeg) -> bool
    {
        if (fromSeg == toSeg) return false;

        const auto& ts = segments[(size_t) toSeg];
        if (ts.nodeA != node && ts.nodeB != node) return false;

        if (const auto* sw = findSwitch (node))
        {
            if (fromSeg == sw->stemSegment)
                return toSeg == sw->normalSegment || toSeg == sw->reverseSegment;
            if (fromSeg == sw->normalSegment || fromSeg == sw->reverseSegment)
                return toSeg == sw->stemSegment;
            return false;
        }

        for (const auto& cr : crossings)
        {
            if (cr.node != node) continue;
            if (fromSeg == cr.pairA1) return toSeg == cr.pairA2;
            if (fromSeg == cr.pairA2) return toSeg == cr.pairA1;
            if (fromSeg == cr.pairB1) return toSeg == cr.pairB2;
            if (fromSeg == cr.pairB2) return toSeg == cr.pairB1;
            return false;
        }

        return true;
    };

    // State: (segment, which end we'll exit from next)
    // We encode state as (segment * 2 + endIdx) where endIdx 0=nodeA, 1=nodeB
    int numSegs = (int) segments.size();
    int numStates = numSegs * 2;

    std::vector<int> prev (numStates, -1);
    std::vector<bool> visited (numStates, false);
    std::queue<int> q;

    auto stateId = [&] (int seg, int end) { return seg * 2 + end; };

    // Seed: start segment, both directions
    auto seedEnd = [&] (int endIdx)
    {
        int sid = stateId (startSeg, endIdx);
        if (! visited[sid]) { visited[sid] = true; prev[sid] = sid; q.push (sid); }
    };
    seedEnd (0);
    seedEnd (1);

    int goalState = -1;

    while (! q.empty())
    {
        int cur = q.front(); q.pop();
        int curSeg = cur / 2;
        int curEnd = cur % 2;

        const auto& cs = segments[(size_t) curSeg];
        int exitNode = (curEnd == 0) ? cs.nodeA : cs.nodeB;

        // Try all segments at exitNode
        for (int i = 0; i < numSegs; ++i)
        {
            if (! canTraverse (curSeg, exitNode, i)) continue;

            const auto& ns = segments[(size_t) i];
            // We entered segment i from exitNode. Which end is the OTHER end?
            int entryEnd = (ns.nodeA == exitNode) ? 0 : 1;
            int otherEnd = 1 - entryEnd;

            // State: on segment i, will exit from otherEnd
            int nid = stateId (i, otherEnd);
            if (visited[nid]) continue;
            visited[nid] = true;
            prev[nid] = cur;
            q.push (nid);

            if (i == targetSeg)
            {
                goalState = nid;
                goto found;
            }

            // Also consider: we enter segment i and DON'T cross it — we reverse.
            // This lets the AI go: leg→stem→(reverse on stem)→back through switch→other leg
            int revId = stateId (i, entryEnd);
            if (! visited[revId])
            {
                visited[revId] = true;
                prev[revId] = cur;
                q.push (revId);

                if (i == targetSeg)
                {
                    goalState = revId;
                    goto found;
                }
            }
        }
    }

    found:
    if (goalState < 0)
        return {};

    // Trace back
    std::vector<int> pathSegs;
    int walk = goalState;
    while (walk != prev[walk])
    {
        pathSegs.push_back (walk / 2);
        walk = prev[walk];
    }
    pathSegs.push_back (walk / 2);
    std::reverse (pathSegs.begin(), pathSegs.end());

    // Remove consecutive duplicates (from reversal states)
    std::vector<int> cleaned;
    for (int s : pathSegs)
        if (cleaned.empty() || cleaned.back() != s)
            cleaned.push_back (s);

    // First direction
    int firstDir = 1;
    if (cleaned.size() >= 2)
    {
        const auto& ss = segments[(size_t) startSeg];
        const auto& ns = segments[(size_t) cleaned[1]];
        if (ns.nodeA == ss.nodeB || ns.nodeB == ss.nodeB)
            firstDir = 1;
        else
            firstDir = -1;
    }
    else
    {
        firstDir = (targetPos.distance > startPos.distance) ? 1 : -1;
    }

    return { cleaned, firstDir };
}

void buildDefaultYard (TrackGraph& graph)
{
    // kept as fallback — load JSON for real levels
    int n0 = graph.addNode ({ -5.0f, 0.0f });
    int n1 = graph.addNode ({ 40.0f, 0.0f });
    graph.addSegment (n0, n1);
}

} // namespace game
