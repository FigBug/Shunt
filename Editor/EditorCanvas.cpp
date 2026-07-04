#include "EditorCanvas.h"
#include <limits>

// ============================================================================
EditorCanvas::EditorCanvas (LevelDocument& d) : doc (d)
{
    setWantsKeyboardFocus (true);
}

void EditorCanvas::setTool (Tool t)
{
    tool = t;
    chainNode = -1;
    hoverSnapNode = -1;
    hoverSplitValid = false;
    if (tool != Tool::select)
    {
        selKind = SelKind::none;
        selId = -1;
    }
    if (onStatusChanged) onStatusChanged();
    if (onToolChanged)   onToolChanged();
    repaint();
}

// ---- view transform --------------------------------------------------------

juce::AffineTransform EditorCanvas::toScreen() const
{
    return juce::AffineTransform::scale (zoom).translated (pan.x, pan.y);
}

juce::Point<float> EditorCanvas::worldToScreen (juce::Point<float> p) const
{
    return p.transformedBy (toScreen());
}

juce::Point<float> EditorCanvas::screenToWorld (juce::Point<float> s) const
{
    return s.transformedBy (toScreen().inverted());
}

juce::Point<float> EditorCanvas::snap (juce::Point<float> w) const
{
    if (! snapGrid) return w;
    return { std::round (w.x / gridSize) * gridSize,
             std::round (w.y / gridSize) * gridSize };
}

// ---- hit testing -----------------------------------------------------------

int EditorCanvas::hitNode (juce::Point<float> world, float screenRadius) const
{
    float r = screenRadius / zoom;
    int best = -1;
    float bestD = r;
    for (const auto& n : doc.nodes)
    {
        float d = n.pos.getDistanceFrom (world);
        if (d < bestD) { bestD = d; best = n.id; }
    }
    return best;
}

int EditorCanvas::hitControl (juce::Point<float> world, float screenRadius) const
{
    if (tool != Tool::select) return -1;
    float r = screenRadius / zoom;
    for (const auto& s : doc.segments)
    {
        if (! s.curved) continue;
        if (s.control.getDistanceFrom (world) < r)
            return s.id;
    }
    return -1;
}

int EditorCanvas::hitSegment (juce::Point<float> world, float screenRadius) const
{
    float r = screenRadius / zoom;
    int best = -1;
    float bestD = r;
    for (const auto& s : doc.segments)
    {
        auto poly = doc.polylineFor (s);
        for (size_t i = 0; i + 1 < poly.size(); ++i)
        {
            juce::Line<float> seg (poly[i], poly[i + 1]);
            auto closest = seg.getPointAlongLineProportionally (
                juce::jlimit (0.0f, 1.0f,
                    [&]
                    {
                        auto d = seg.getEnd() - seg.getStart();
                        float len2 = d.x * d.x + d.y * d.y;
                        if (len2 <= 0.0f) return 0.0f;
                        auto w = world - seg.getStart();
                        return (w.x * d.x + w.y * d.y) / len2;
                    }()));
            float dist = closest.getDistanceFrom (world);
            if (dist < bestD) { bestD = dist; best = s.id; }
        }
    }
    return best;
}

int EditorCanvas::hitSpawn (juce::Point<float> world, float screenRadius) const
{
    float r = screenRadius / zoom;
    int best = -1;
    float bestD = r;
    for (int i = 0; i < (int) doc.spawns.size(); ++i)
    {
        float d = doc.spawns[(size_t) i].pos.getDistanceFrom (world);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int EditorCanvas::snapNodeAt (juce::Point<float> world, int excludeId) const
{
    float r = kSnapPx / zoom;
    int best = -1;
    float bestD = r;
    for (const auto& n : doc.nodes)
    {
        if (n.id == excludeId) continue;
        float d = n.pos.getDistanceFrom (world);
        if (d < bestD) { bestD = d; best = n.id; }
    }
    return best;
}

bool EditorCanvas::nearestSegment (juce::Point<float> world, int excludeNode,
                                   int& outSeg, float& outT,
                                   juce::Point<float>& outPoint, float& outWorldDist) const
{
    bool found = false;
    outWorldDist = std::numeric_limits<float>::max();

    for (const auto& s : doc.segments)
    {
        if (excludeNode >= 0 && (s.from == excludeNode || s.to == excludeNode))
            continue;

        auto poly = doc.polylineFor (s);
        int m = (int) poly.size();
        for (int i = 0; i + 1 < m; ++i)
        {
            auto a = poly[(size_t) i], b = poly[(size_t) (i + 1)];
            auto d = b - a;
            float len2 = d.x * d.x + d.y * d.y;
            float lt = len2 > 0.0f
                ? juce::jlimit (0.0f, 1.0f, ((world.x - a.x) * d.x + (world.y - a.y) * d.y) / len2)
                : 0.0f;
            auto p = a + d * lt;
            float dist = p.getDistanceFrom (world);
            if (dist < outWorldDist)
            {
                outWorldDist = dist;
                outPoint = p;
                outSeg = s.id;
                outT = ((float) i + lt) / (float) (m - 1);
                found = true;
            }
        }
    }
    return found;
}

bool EditorCanvas::nearestSegmentPoint (juce::Point<float> world,
                                        juce::Point<float>& outPoint, float& outWorldDist) const
{
    int seg; float t;
    return nearestSegment (world, -1, seg, t, outPoint, outWorldDist);
}

int EditorCanvas::findOrCreateNode (juce::Point<float> world)
{
    if (int existing = hitNode (world); existing >= 0)
        return existing;

    // Landing on the interior of an existing track splits it there, forming a
    // real junction/switch instead of a disconnected overlapping node.
    int seg; float t; juce::Point<float> pt; float dist;
    if (nearestSegment (world, -1, seg, t, pt, dist) && dist * zoom <= kSnapPx)
        return doc.splitSegment (seg, t);

    return doc.addNode (snap (world));
}

// ============================================================================
// Mouse
// ============================================================================

void EditorCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto world = screenToWorld (e.position);

    // Right button (or middle) always pans.
    if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown())
    {
        panning = true;
        panStart = e.position;
        panOrigin = pan;
        return;
    }

    switch (tool)
    {
        case Tool::select:
        {
            if (int c = hitControl (world); c >= 0)
            {
                selKind = SelKind::control; selId = c; dragging = true;
            }
            else if (int n = hitNode (world); n >= 0)
            {
                selKind = SelKind::node; selId = n; dragging = true;
            }
            else if (int sp = hitSpawn (world); sp >= 0)
            {
                selKind = SelKind::spawn; selId = sp; dragging = true;
            }
            else if (int s = hitSegment (world); s >= 0)
            {
                selKind = SelKind::segment; selId = s; dragging = false;
            }
            else
            {
                selKind = SelKind::none; selId = -1;
            }
            break;
        }

        case Tool::straight:
        case Tool::curve:
        {
            int node = findOrCreateNode (world);
            if (chainNode < 0)
            {
                chainNode = node;
            }
            else if (node != chainNode)
            {
                doc.addSegment (chainNode, node, tool == Tool::curve);
                chainNode = node;   // continue chaining from the new node
            }
            break;
        }

        case Tool::rect:
        {
            rectDragging = true;
            rectStart = rectEnd = snap (world);
            break;
        }

        case Tool::spawn:
        {
            juce::Point<float> onTrack;
            float dist;
            auto pos = snap (world);
            if (nearestSegmentPoint (world, onTrack, dist) && dist * zoom <= kSnapPx)
                pos = onTrack;                // drop it exactly on the nearest rail
            doc.spawns.push_back ({ pos, -1 });
            break;
        }

        case Tool::dropOff:
        {
            int n = hitNode (world);
            if (n < 0)
            {
                // Drop-offs must sit on the track (the game spawns engines and
                // scores deliveries at the drop-off node). Snap onto the nearest
                // segment, splitting it; only fall back to a free node if there
                // is no track nearby.
                int seg; float t; juce::Point<float> pt; float dist;
                if (nearestSegment (world, -1, seg, t, pt, dist) && dist * zoom <= kSnapPx)
                    n = doc.splitSegment (seg, t);
                else
                    n = doc.addNode (snap (world));
            }

            if (const auto* d = doc.dropOffForNode (n); d != nullptr && d->colour == dropColour)
                doc.clearDropOff (n);             // click same colour again to remove
            else
                doc.setDropOff (n, dropColour);
            break;
        }

        case Tool::erase:
        {
            if (int sp = hitSpawn (world); sp >= 0)
                doc.spawns.erase (doc.spawns.begin() + sp);
            else if (int n = hitNode (world); n >= 0)
                doc.removeNode (n);
            else if (int s = hitSegment (world); s >= 0)
                doc.removeSegment (s);
            break;
        }
    }

    updateStatus (world);
    repaint();
}

void EditorCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (panning)
    {
        pan = panOrigin + (e.position - panStart);
        repaint();
        return;
    }

    if (rectDragging)
    {
        rectEnd = snap (screenToWorld (e.position));
        updateStatus (rectEnd);
        repaint();
        return;
    }

    if (! dragging) return;
    auto world = snap (screenToWorld (e.position));

    switch (selKind)
    {
        case SelKind::node:
            if (auto* n = doc.findNode (selId))
            {
                // Snap onto another node within range — dropping here merges the
                // two (joining tracks into a switch/junction).
                hoverSnapNode = snapNodeAt (world, selId);
                n->pos = (hoverSnapNode >= 0) ? doc.findNode (hoverSnapNode)->pos : world;
            }
            break;
        case SelKind::control:
            if (auto* s = doc.findSegment (selId)) s->control = screenToWorld (e.position);
            break;
        case SelKind::spawn:
            if (selId >= 0 && selId < (int) doc.spawns.size())
            {
                juce::Point<float> onTrack;
                float dist;
                auto pos = world;
                if (nearestSegmentPoint (world, onTrack, dist) && dist * zoom <= kSnapPx)
                    pos = onTrack;            // slide along the nearest rail
                doc.spawns[(size_t) selId].pos = pos;
            }
            break;
        case SelKind::none:
        case SelKind::segment:
            break;
    }
    updateStatus (world);
    repaint();
}

float EditorCanvas::rectRadiusFor (juce::Rectangle<float> b) const
{
    // 25% of the shorter side is always within [0, half the shorter side], so
    // no clamping is needed. (A degenerate 0-size rect during the rubber-band
    // drag just yields radius 0, which draws a plain rectangle preview.)
    return juce::jmin (b.getWidth(), b.getHeight()) * 0.25f;
}

void EditorCanvas::mouseUp (const juce::MouseEvent&)
{
    if (rectDragging)
    {
        rectDragging = false;
        juce::Rectangle<float> b (rectStart, rectEnd);
        if (b.getWidth() > 4.0f && b.getHeight() > 4.0f)
            doc.addRoundedRectLoop (b, rectRadiusFor (b));
        repaint();
    }
    // Resolve a node drag: merge onto another node, or split a segment it was
    // dropped on — either way forming a real junction/switch.
    if (dragging && selKind == SelKind::node)
    {
        if (hoverSnapNode >= 0)
        {
            doc.mergeNode (selId, hoverSnapNode);
            selId = hoverSnapNode;
        }
        else if (auto* n = doc.findNode (selId))
        {
            int seg; float t; juce::Point<float> pt; float dist;
            if (nearestSegment (n->pos, selId, seg, t, pt, dist) && dist * zoom <= kSnapPx)
                doc.splitSegment (seg, t, selId);   // dragged node becomes the junction
        }
    }

    hoverSnapNode = -1;
    hoverSplitValid = false;
    panning = false;
    dragging = false;
    repaint();
}

void EditorCanvas::mouseMove (const juce::MouseEvent& e)
{
    lastMouseWorld = screenToWorld (e.position);

    // For the connect-y tools, preview whether a click will join an existing
    // node (green ring) or split a track to form a junction (orange marker).
    bool connectTool = (tool == Tool::straight || tool == Tool::curve || tool == Tool::dropOff);
    hoverSnapNode = connectTool ? snapNodeAt (lastMouseWorld, -1) : -1;

    hoverSplitValid = false;
    if (connectTool && hoverSnapNode < 0)
    {
        int seg; float t; juce::Point<float> pt; float dist;
        if (nearestSegment (lastMouseWorld, -1, seg, t, pt, dist) && dist * zoom <= kSnapPx)
        {
            hoverSplitValid = true;
            hoverSplitPoint = pt;
        }
    }

    updateStatus (lastMouseWorld);
    repaint();
}

void EditorCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto before = screenToWorld (e.position);
    float factor = std::exp (w.deltaY * 1.2f);
    zoom = juce::jlimit (0.05f, 20.0f, zoom * factor);
    // keep the point under the cursor fixed
    auto after = screenToWorld (e.position);
    pan += (after - before) * zoom;
    repaint();
}

bool EditorCanvas::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        chainNode = -1;
        selKind = SelKind::none; selId = -1;
        repaint();
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        switch (selKind)
        {
            case SelKind::node:    doc.removeNode (selId); break;
            case SelKind::segment: doc.removeSegment (selId); break;
            case SelKind::spawn:
                if (selId >= 0 && selId < (int) doc.spawns.size())
                    doc.spawns.erase (doc.spawns.begin() + selId);
                break;
            case SelKind::none:
            case SelKind::control:
                break;
        }
        selKind = SelKind::none; selId = -1;
        repaint();
        return true;
    }
    if (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G')
    {
        snapGrid = ! snapGrid;
        updateStatus (lastMouseWorld);
        repaint();
        return true;
    }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F')
    {
        frameAll();
        return true;
    }

    // Tool hotkeys
    switch (juce::CharacterFunctions::toUpperCase (k.getTextCharacter()))
    {
        case 'V': setTool (Tool::select);   return true;
        case 'L': setTool (Tool::straight); return true;
        case 'C': setTool (Tool::curve);    return true;
        case 'R': setTool (Tool::rect);     return true;
        case 'S': setTool (Tool::spawn);    return true;
        case 'D': setTool (Tool::dropOff);  return true;
        case 'E': setTool (Tool::erase);    return true;
        default: break;
    }
    return false;
}

void EditorCanvas::updateStatus (juce::Point<float> world)
{
    static const char* toolNames[] = { "Select", "Straight", "Curve", "Rect", "Spawn", "Drop-off", "Erase" };
    statusText = juce::String (toolNames[(int) tool])
               + "   x: " + juce::String (world.x, 1)
               + "  y: " + juce::String (world.y, 1)
               + "   zoom: " + juce::String (zoom, 2) + "x"
               + (snapGrid ? "   [grid]" : "")
               + "   nodes: " + juce::String ((int) doc.nodes.size())
               + "  edges: " + juce::String ((int) doc.segments.size());
    if (onStatusChanged) onStatusChanged();
}

// ============================================================================
// View helpers
// ============================================================================

void EditorCanvas::frameAll()
{
    if (doc.nodes.empty() && doc.spawns.empty())
    {
        zoom = 1.0f;
        pan = { 40.0f, 40.0f };
        repaint();
        return;
    }

    // Accumulate the bounding box by hand — Rectangle::getUnion treats a
    // zero-size rect as empty and drops it, so unioning point-rects collapses.
    auto first = doc.nodes.empty() ? doc.spawns.front().pos : doc.nodes.front().pos;
    float minX = first.x, minY = first.y, maxX = first.x, maxY = first.y;
    auto include = [&] (juce::Point<float> p)
    {
        minX = juce::jmin (minX, p.x);  minY = juce::jmin (minY, p.y);
        maxX = juce::jmax (maxX, p.x);  maxY = juce::jmax (maxY, p.y);
    };
    for (const auto& n : doc.nodes)   include (n.pos);
    for (const auto& sp : doc.spawns) include (sp.pos);

    juce::Rectangle<float> bounds (minX, minY, maxX - minX, maxY - minY);

    // Pad, guaranteeing a non-zero extent even for a single point or an
    // axis-aligned line (expanded adds the delta to every side).
    bounds = bounds.expanded (juce::jmax (bounds.getWidth(), bounds.getHeight()) * 0.08f + 20.0f);

    auto area = getLocalBounds().toFloat().reduced (10.0f);
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f || area.isEmpty())
        return;

    zoom = juce::jlimit (0.05f, 20.0f,
                         juce::jmin (area.getWidth() / bounds.getWidth(),
                                     area.getHeight() / bounds.getHeight()));
    pan = area.getCentre() - bounds.getCentre() * zoom;
    repaint();
}

int EditorCanvas::weldLooseEnds()
{
    // ~12 screen px at the current zoom: catches ends drawn "touching" without
    // fusing genuinely separate tracks.
    int n = doc.weldLooseEnds (12.0f / juce::jmax (0.01f, zoom));
    repaint();
    return n;
}

void EditorCanvas::resized() {}

// ============================================================================
// Painting
// ============================================================================

void EditorCanvas::drawGrid (juce::Graphics& g)
{
    auto worldTL = screenToWorld ({ 0.0f, 0.0f });
    auto worldBR = screenToWorld ({ (float) getWidth(), (float) getHeight() });

    float spacing = gridSize;
    while (spacing * zoom < 8.0f)  spacing *= 5.0f;   // thin out when zoomed far out

    g.setColour (juce::Colour::fromRGB (44, 48, 54));
    for (float x = std::floor (worldTL.x / spacing) * spacing; x < worldBR.x; x += spacing)
    {
        float sx = worldToScreen ({ x, 0.0f }).x;
        g.drawVerticalLine ((int) sx, 0.0f, (float) getHeight());
    }
    for (float y = std::floor (worldTL.y / spacing) * spacing; y < worldBR.y; y += spacing)
    {
        float sy = worldToScreen ({ 0.0f, y }).y;
        g.drawHorizontalLine ((int) sy, 0.0f, (float) getWidth());
    }

    // origin axes
    g.setColour (juce::Colour::fromRGB (70, 76, 84));
    auto o = worldToScreen ({ 0.0f, 0.0f });
    g.drawVerticalLine ((int) o.x, 0.0f, (float) getHeight());
    g.drawHorizontalLine ((int) o.y, 0.0f, (float) getWidth());
}

void EditorCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (30, 33, 38));
    drawGrid (g);

    auto xf = toScreen();

    // ---- track segments ----
    for (const auto& s : doc.segments)
    {
        auto poly = doc.polylineFor (s);
        if (poly.size() < 2) continue;

        juce::Path path;
        path.startNewSubPath (poly.front());
        for (size_t i = 1; i < poly.size(); ++i)
            path.lineTo (poly[i]);
        path.applyTransform (xf);

        bool selected = (selKind == SelKind::segment && selId == s.id);

        // ties / bed
        g.setColour (juce::Colour::fromRGB (90, 96, 104));
        g.strokePath (path, juce::PathStrokeType (juce::jmax (5.0f, 7.0f),
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // rail
        g.setColour (selected ? juce::Colours::yellow : juce::Colour::fromRGB (200, 205, 212));
        g.strokePath (path, juce::PathStrokeType (juce::jmax (1.5f, 2.5f),
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // curve control handle
        if (s.curved && tool == Tool::select)
        {
            auto* a = doc.findNode (s.from);
            auto* b = doc.findNode (s.to);
            if (a && b)
            {
                auto ca = worldToScreen (a->pos);
                auto cc = worldToScreen (s.control);
                auto cb = worldToScreen (b->pos);
                g.setColour (juce::Colours::skyblue.withAlpha (0.4f));
                g.drawLine ({ ca, cc }, 1.0f);
                g.drawLine ({ cc, cb }, 1.0f);
                bool selCtrl = (selKind == SelKind::control && selId == s.id);
                g.setColour (selCtrl ? juce::Colours::yellow : juce::Colours::skyblue);
                g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (cc));
            }
        }
    }

    // ---- drop-offs (rings under nodes) ----
    for (const auto& d : doc.dropOffs)
    {
        if (const auto* n = doc.findNode (d.node))
        {
            auto c = worldToScreen (n->pos);
            g.setColour (LevelDocument::slotColour (d.colour));
            float r = 13.0f;
            g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c), 3.0f);
        }
    }

    // ---- nodes ----
    for (const auto& n : doc.nodes)
    {
        auto c = worldToScreen (n.pos);
        int deg = doc.nodeDegree (n.id);
        float r = deg >= 3 ? 6.0f : (deg == 1 ? 5.0f : 4.0f);

        juce::Colour col = deg >= 3 ? juce::Colour::fromRGB (255, 200, 80)   // switch
                         : deg == 1 ? juce::Colour::fromRGB (120, 220, 120)   // endpoint
                                    : juce::Colour::fromRGB (160, 170, 180);  // junction
        bool selected = (selKind == SelKind::node && selId == n.id);
        g.setColour (selected ? juce::Colours::yellow : col);
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c), 1.0f);
    }

    // ---- snap target highlight (green ring on the node a click/drag joins) ----
    if (hoverSnapNode >= 0)
    {
        if (const auto* n = doc.findNode (hoverSnapNode))
        {
            auto c = worldToScreen (n->pos);
            g.setColour (juce::Colour::fromRGB (60, 220, 90));
            g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c), 2.5f);
        }
    }

    // ---- split preview (orange marker where a click would break a track to
    //      form a junction/switch) ----
    if (hoverSplitValid)
    {
        auto c = worldToScreen (hoverSplitPoint);
        g.setColour (juce::Colour::fromRGB (255, 170, 40));
        g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (c), 2.5f);
        g.drawLine (c.x - 6, c.y, c.x + 6, c.y, 2.0f);
        g.drawLine (c.x, c.y - 6, c.x, c.y + 6, 2.0f);
    }

    // ---- spawns (green = on a track, red = floating off-track) ----
    for (int i = 0; i < (int) doc.spawns.size(); ++i)
    {
        auto spawnPos = doc.spawns[(size_t) i].pos;
        auto c = worldToScreen (spawnPos);
        bool selected = (selKind == SelKind::spawn && selId == i);

        juce::Point<float> onTrack;
        float dist = 0.0f;
        bool has = nearestSegmentPoint (spawnPos, onTrack, dist);
        bool onRail = has && dist * zoom <= 4.0f;   // essentially on the centreline

        // Show where an off-track spawn would snap to in-game.
        if (has && ! onRail)
        {
            g.setColour (juce::Colour::fromRGB (230, 90, 90).withAlpha (0.7f));
            g.drawLine ({ c, worldToScreen (onTrack) }, 1.0f);
        }

        juce::Path diamond;
        float r = 8.0f;
        diamond.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
        juce::Colour fill = selected ? juce::Colours::yellow
                          : onRail    ? juce::Colour::fromRGB (70, 200, 110)
                                      : juce::Colour::fromRGB (230, 90, 90);
        g.setColour (fill);
        g.fillPath (diamond);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
    }

    // ---- in-progress chain preview ----
    if (chainNode >= 0)
    {
        if (const auto* n = doc.findNode (chainNode))
        {
            // Preview endpoint snaps to a nearby node when one is in range.
            auto endWorld = (hoverSnapNode >= 0 && doc.findNode (hoverSnapNode) != nullptr)
                                ? doc.findNode (hoverSnapNode)->pos
                                : snap (lastMouseWorld);
            auto a = worldToScreen (n->pos);
            auto b = worldToScreen (endWorld);
            g.setColour (juce::Colours::yellow.withAlpha (0.7f));
            g.drawLine ({ a, b }, tool == Tool::curve ? 2.5f : 1.5f);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (a));
        }
    }

    // ---- rounded-rect rubber band ----
    if (rectDragging)
    {
        juce::Rectangle<float> b (rectStart, rectEnd);
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), rectRadiusFor (b));
        p.applyTransform (xf);
        g.setColour (juce::Colours::yellow.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}
