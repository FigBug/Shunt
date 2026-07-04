#include "LevelDocument.h"
#include <algorithm>
#include <map>

namespace
{
    constexpr int kCurveSamples = 24;
}

// ============================================================================
// Mutation
// ============================================================================

int LevelDocument::addNode (juce::Point<float> p)
{
    int id = nextNodeId++;
    nodes.push_back ({ id, p });
    return id;
}

int LevelDocument::addSegment (int from, int to, bool curved)
{
    if (from == to)
        return -1;

    // Avoid duplicate segments between the same pair of nodes.
    for (const auto& s : segments)
        if ((s.from == from && s.to == to) || (s.from == to && s.to == from))
            return s.id;

    EdSegment s;
    s.id     = nextSegmentId++;
    s.from   = from;
    s.to     = to;
    s.curved = curved;
    s.control = defaultControlFor (from, to);
    segments.push_back (s);
    return s.id;
}

void LevelDocument::addRoundedRectLoop (juce::Rectangle<float> b, float radius)
{
    const float L = b.getX(), R = b.getRight(), T = b.getY(), B = b.getBottom();
    // Radius must stay within [0, half the shorter side]. Prefer at least 1 unit
    // so corners aren't degenerate, but never let the lower bound exceed the
    // upper (that would trip jlimit's assert on a tiny rect).
    const float maxR = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float r = juce::jlimit (juce::jmin (1.0f, maxR), maxR, radius);

    // Eight tangent points, clockwise from the top-left of the top edge.
    int n0 = addNode ({ L + r, T });
    int n1 = addNode ({ R - r, T });
    int n2 = addNode ({ R,     T + r });
    int n3 = addNode ({ R,     B - r });
    int n4 = addNode ({ R - r, B });
    int n5 = addNode ({ L + r, B });
    int n6 = addNode ({ L,     B - r });
    int n7 = addNode ({ L,     T + r });

    auto corner = [this] (int a, int c, juce::Point<float> ctrl)
    {
        int id = addSegment (a, c, true);
        if (auto* s = findSegment (id)) s->control = ctrl;   // control at the rect corner
    };

    addSegment (n0, n1, false);          // top
    corner     (n1, n2, { R, T });       // top-right
    addSegment (n2, n3, false);          // right
    corner     (n3, n4, { R, B });       // bottom-right
    addSegment (n4, n5, false);          // bottom
    corner     (n5, n6, { L, B });       // bottom-left
    addSegment (n6, n7, false);          // left
    corner     (n7, n0, { L, T });       // top-left
}

void LevelDocument::mergeNode (int fromId, int intoId)
{
    if (fromId == intoId || findNode (fromId) == nullptr || findNode (intoId) == nullptr)
        return;

    // Reattach every segment on `fromId` to `intoId`.
    for (auto& s : segments)
    {
        if (s.from == fromId) s.from = intoId;
        if (s.to   == fromId) s.to   = intoId;
    }

    // Drop self-loops created by the merge.
    segments.erase (std::remove_if (segments.begin(), segments.end(),
        [] (const EdSegment& s) { return s.from == s.to; }),
        segments.end());

    // De-duplicate segments that now connect the same pair of nodes.
    std::vector<EdSegment> kept;
    for (const auto& s : segments)
    {
        bool dup = false;
        for (const auto& k : kept)
            if ((k.from == s.from && k.to == s.to) || (k.from == s.to && k.to == s.from))
                { dup = true; break; }
        if (! dup) kept.push_back (s);
    }
    segments = std::move (kept);

    // Move any drop-off onto the surviving node, then collapse duplicates.
    for (auto& d : dropOffs)
        if (d.node == fromId) d.node = intoId;
    std::vector<EdDropOff> keptDrops;
    for (const auto& d : dropOffs)
    {
        bool dup = false;
        for (const auto& k : keptDrops)
            if (k.node == d.node) { dup = true; break; }
        if (! dup) keptDrops.push_back (d);
    }
    dropOffs = std::move (keptDrops);

    // Finally remove the folded-away node (its segments already moved).
    nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
        [fromId] (const EdNode& n) { return n.id == fromId; }),
        nodes.end());
}

int LevelDocument::splitSegment (int segId, float t, int useNodeId)
{
    auto* s = findSegment (segId);
    if (s == nullptr) return -1;
    const auto* an = findNode (s->from);
    const auto* bn = findNode (s->to);
    if (an == nullptr || bn == nullptr) return -1;

    t = juce::jlimit (0.001f, 0.999f, t);
    const int from = s->from, to = s->to;
    const bool curved = s->curved;
    const auto A = an->pos, B = bn->pos, C = s->control;

    auto lerp = [] (juce::Point<float> p, juce::Point<float> q, float u) { return p + (q - p) * u; };

    juce::Point<float> splitPos, ctrl1, ctrl2;
    if (curved)
    {
        // de Casteljau: split a quadratic bezier at t into two quadratics.
        ctrl1    = lerp (A, C, t);
        ctrl2    = lerp (C, B, t);
        splitPos = lerp (ctrl1, ctrl2, t);
    }
    else
    {
        splitPos = lerp (A, B, t);
    }

    int mid;
    if (useNodeId >= 0 && findNode (useNodeId) != nullptr)
    {
        mid = useNodeId;
        findNode (useNodeId)->pos = splitPos;   // pull the node exactly onto the track
    }
    else
    {
        mid = addNode (splitPos);
    }

    // addNode may have reallocated; re-find the segment before mutating it.
    s = findSegment (segId);
    s->to = mid;
    if (curved) s->control = ctrl1;

    EdSegment ns;
    ns.id      = nextSegmentId++;
    ns.from    = mid;
    ns.to      = to;
    ns.curved  = curved;
    ns.control = ctrl2;
    segments.push_back (ns);

    juce::ignoreUnused (from);
    return mid;
}

int LevelDocument::weldLooseEnds (float tolerance)
{
    int welded = 0;

    std::vector<int> ids;
    ids.reserve (nodes.size());
    for (const auto& n : nodes) ids.push_back (n.id);

    for (int nid : ids)
    {
        if (nodeDegree (nid) > 1) continue;        // only loose ends / isolated nodes
        const auto* n = findNode (nid);
        if (n == nullptr) continue;
        auto p = n->pos;

        int   bestSeg = -1;
        float bestDist = tolerance;
        float bestT = 0.0f;

        for (const auto& s : segments)
        {
            if (s.from == nid || s.to == nid) continue;   // skip incident segments
            auto poly = polylineFor (s);
            int m = (int) poly.size();
            for (int i = 0; i + 1 < m; ++i)
            {
                auto a = poly[(size_t) i], b = poly[(size_t) (i + 1)];
                auto d = b - a;
                float len2 = d.x * d.x + d.y * d.y;
                float lt = len2 > 0.0f
                    ? juce::jlimit (0.0f, 1.0f, ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2)
                    : 0.0f;
                auto pr = a + d * lt;
                float dist = pr.getDistanceFrom (p);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestSeg  = s.id;
                    bestT    = ((float) i + lt) / (float) (m - 1);
                }
            }
        }

        if (bestSeg >= 0)
        {
            splitSegment (bestSeg, bestT, nid);
            ++welded;
        }
    }

    return welded;
}

void LevelDocument::removeNode (int id)
{
    segments.erase (std::remove_if (segments.begin(), segments.end(),
        [id] (const EdSegment& s) { return s.from == id || s.to == id; }),
        segments.end());

    dropOffs.erase (std::remove_if (dropOffs.begin(), dropOffs.end(),
        [id] (const EdDropOff& d) { return d.node == id; }),
        dropOffs.end());

    nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
        [id] (const EdNode& n) { return n.id == id; }),
        nodes.end());
}

void LevelDocument::removeSegment (int id)
{
    segments.erase (std::remove_if (segments.begin(), segments.end(),
        [id] (const EdSegment& s) { return s.id == id; }),
        segments.end());
}

void LevelDocument::clear()
{
    nodes.clear();
    segments.clear();
    dropOffs.clear();
    spawns.clear();
    totalCars = 20;
    nextNodeId = 0;
    nextSegmentId = 0;
}

// ============================================================================
// Lookup
// ============================================================================

EdNode* LevelDocument::findNode (int id)
{
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const EdNode* LevelDocument::findNode (int id) const
{
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

EdSegment* LevelDocument::findSegment (int id)
{
    for (auto& s : segments)
        if (s.id == id) return &s;
    return nullptr;
}

int LevelDocument::nodeDegree (int id) const
{
    int d = 0;
    for (const auto& s : segments)
        if (s.from == id || s.to == id) ++d;
    return d;
}

const EdDropOff* LevelDocument::dropOffForNode (int nodeId) const
{
    for (const auto& d : dropOffs)
        if (d.node == nodeId) return &d;
    return nullptr;
}

void LevelDocument::setDropOff (int nodeId, int colour)
{
    for (auto& d : dropOffs)
        if (d.node == nodeId) { d.colour = colour; return; }
    dropOffs.push_back ({ colour, nodeId });
}

void LevelDocument::clearDropOff (int nodeId)
{
    dropOffs.erase (std::remove_if (dropOffs.begin(), dropOffs.end(),
        [nodeId] (const EdDropOff& d) { return d.node == nodeId; }),
        dropOffs.end());
}

// ============================================================================
// Geometry
// ============================================================================

juce::Point<float> LevelDocument::defaultControlFor (int fromNode, int toNode) const
{
    const auto* a = findNode (fromNode);
    const auto* b = findNode (toNode);
    if (a == nullptr || b == nullptr)
        return {};

    auto mid = (a->pos + b->pos) * 0.5f;
    auto dir = b->pos - a->pos;
    auto len = dir.getDistanceFromOrigin();
    if (len <= 0.0f)
        return mid;

    // Bow the default control out perpendicular to the chord so a fresh curve
    // is visibly curved and immediately grab-able.
    juce::Point<float> perp (-dir.y / len, dir.x / len);
    return mid + perp * (len * 0.25f);
}

std::vector<juce::Point<float>> LevelDocument::polylineFor (const EdSegment& s) const
{
    std::vector<juce::Point<float>> poly;
    const auto* a = findNode (s.from);
    const auto* b = findNode (s.to);
    if (a == nullptr || b == nullptr)
        return poly;

    if (! s.curved)
    {
        poly.push_back (a->pos);
        poly.push_back (b->pos);
        return poly;
    }

    for (int i = 0; i <= kCurveSamples; ++i)
    {
        float t = (float) i / (float) kCurveSamples;
        float u = 1.0f - t;
        auto p = a->pos * (u * u)
               + s.control * (2.0f * u * t)
               + b->pos * (t * t);
        poly.push_back (p);
    }
    return poly;
}

float LevelDocument::lengthFor (const EdSegment& s) const
{
    auto poly = polylineFor (s);
    float len = 0.0f;
    for (size_t i = 0; i + 1 < poly.size(); ++i)
        len += poly[i].getDistanceFrom (poly[i + 1]);
    return len;
}

// ============================================================================
// Colours (mirrors GameState kSlotColours)
// ============================================================================

juce::Colour LevelDocument::slotColour (int index)
{
    switch (index)
    {
        case 0:  return juce::Colour::fromRGB (255, 140,   0);   // orange
        case 1:  return juce::Colour::fromRGB (180,  50, 200);   // purple
        case 2:  return juce::Colour::fromRGB (  0, 200, 200);   // cyan
        case 3:  return juce::Colour::fromRGB (200, 200, 200);   // white/silver
        default: return juce::Colours::grey;
    }
}

// ============================================================================
// Serialisation
// ============================================================================

juce::String LevelDocument::toJsonString() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();

    // nodes
    juce::Array<juce::var> nodeArr;
    for (const auto& n : nodes)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        int deg = nodeDegree (n.id);
        o->setProperty ("id", n.id);
        o->setProperty ("x", n.pos.x);
        o->setProperty ("y", n.pos.y);
        o->setProperty ("kind", deg == 1 ? "endpoint" : (deg >= 3 ? "switch" : "junction"));
        o->setProperty ("degree", deg);
        nodeArr.add (juce::var (o.get()));
    }
    root->setProperty ("nodes", nodeArr);

    // edges
    juce::Array<juce::var> edgeArr;
    for (const auto& s : segments)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("id", s.id);
        o->setProperty ("from", s.from);
        o->setProperty ("to", s.to);

        juce::Array<juce::var> poly;
        for (auto& p : polylineFor (s))
        {
            juce::Array<juce::var> pt;
            pt.add (p.x);
            pt.add (p.y);
            poly.add (pt);
        }
        o->setProperty ("polyline", poly);
        o->setProperty ("length", lengthFor (s));
        o->setProperty ("has_signal", false);
        o->setProperty ("buffer_end", nodeDegree (s.from) == 1 || nodeDegree (s.to) == 1);
        o->setProperty ("curved", s.curved);
        o->setProperty ("control_x", s.control.x);
        o->setProperty ("control_y", s.control.y);
        edgeArr.add (juce::var (o.get()));
    }
    root->setProperty ("edges", edgeArr);

    // drop-offs
    juce::Array<juce::var> dropArr;
    for (const auto& d : dropOffs)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("colour", d.colour);
        o->setProperty ("node", d.node);
        dropArr.add (juce::var (o.get()));
    }
    root->setProperty ("drop_offs", dropArr);

    // spawns (editor extension)
    juce::Array<juce::var> spawnArr;
    for (const auto& sp : spawns)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("x", sp.pos.x);
        o->setProperty ("y", sp.pos.y);
        o->setProperty ("colour", sp.colour);
        spawnArr.add (juce::var (o.get()));
    }
    root->setProperty ("spawns", spawnArr);

    root->setProperty ("car_count", totalCars);

    return juce::JSON::toString (juce::var (root.get()), false);
}

bool LevelDocument::loadFromString (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    if (! root.isObject())
        return false;

    clear();

    int maxNodeId = -1;
    int maxSegId  = -1;

    // Nodes
    if (auto* arr = root.getProperty ("nodes", {}).getArray())
    {
        for (const auto& nv : *arr)
        {
            EdNode n;
            n.id    = (int) nv.getProperty ("id", 0);
            n.pos.x = (float) (double) nv.getProperty ("x", 0.0);
            n.pos.y = (float) (double) nv.getProperty ("y", 0.0);
            nodes.push_back (n);
            maxNodeId = juce::jmax (maxNodeId, n.id);
        }
    }

    // Edges — reconstruct straight/curve from stored hints, falling back to the
    // polyline for levels authored elsewhere.
    if (auto* arr = root.getProperty ("edges", {}).getArray())
    {
        for (const auto& ev : *arr)
        {
            EdSegment s;
            s.id   = (int) ev.getProperty ("id", 0);
            s.from = (int) ev.getProperty ("from", -1);

            std::vector<juce::Point<float>> poly;
            if (auto* pa = ev.getProperty ("polyline", {}).getArray())
                for (const auto& pv : *pa)
                    if (auto* pt = pv.getArray())
                        poly.push_back ({ (float) (double) (*pt)[0],
                                          (float) (double) (*pt)[1] });

            // Resolve the destination node. Buffer-ended edges (no "to") get a
            // synthesised endpoint node at the polyline tail.
            int toId = -1;
            auto toProp = ev.getProperty ("to", {});
            if (! toProp.isVoid())
                toId = (int) toProp;

            if (toId < 0)
            {
                if (! poly.empty())
                {
                    toId = ++maxNodeId;
                    nodes.push_back ({ toId, poly.back() });
                }
                else
                {
                    continue;
                }
            }
            s.to = toId;

            if (s.from < 0)
                continue;

            if (ev.hasProperty ("curved"))
            {
                // Level authored in this editor — geometry stored losslessly.
                s.curved  = (bool) ev.getProperty ("curved", false);
                s.control = { (float) (double) ev.getProperty ("control_x", 0.0),
                              (float) (double) ev.getProperty ("control_y", 0.0) };
            }
            else
            {
                // Imported from elsewhere: a multi-point polyline is a curve;
                // approximate its control point from the mid polyline vertex so
                // it stays editable (re-export regenerates a quadratic).
                s.curved = poly.size() > 2;
                if (s.curved)
                {
                    auto mid = poly[poly.size() / 2];
                    const auto* a = findNode (s.from);
                    const auto* b = findNode (s.to);   // exists (pre-existing or just synthesised)
                    s.control = (a != nullptr && b != nullptr)
                                    ? mid * 2.0f - (a->pos + b->pos) * 0.5f
                                    : mid;
                }
            }

            segments.push_back (s);
            maxSegId  = juce::jmax (maxSegId, s.id);
            maxNodeId = juce::jmax (maxNodeId, juce::jmax (s.from, s.to));
        }
    }

    nextNodeId    = maxNodeId + 1;
    nextSegmentId = maxSegId + 1;

    // Drop-offs
    if (auto* arr = root.getProperty ("drop_offs", {}).getArray())
        for (const auto& dv : *arr)
            dropOffs.push_back ({ (int) dv.getProperty ("colour", 0),
                                  (int) dv.getProperty ("node", -1) });

    // Spawns (editor extension)
    if (auto* arr = root.getProperty ("spawns", {}).getArray())
        for (const auto& sv : *arr)
            spawns.push_back ({ { (float) (double) sv.getProperty ("x", 0.0),
                                  (float) (double) sv.getProperty ("y", 0.0) },
                                (int) sv.getProperty ("colour", -1) });

    // Total car count; default to one per spawn for levels authored before it.
    totalCars = root.hasProperty ("car_count")
                  ? (int) root.getProperty ("car_count", 0)
                  : (int) spawns.size();

    return true;
}
