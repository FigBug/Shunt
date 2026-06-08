#include "GameView.h"
#include <cmath>

namespace view
{

GameView::GameView (game::GameState& s) : state (s) {}

void GameView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (60, 75, 60));

    const auto& track = state.getTrack();

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < track.numNodes(); ++i)
    {
        auto p = track.getNode (i).position;
        minX = juce::jmin (minX, p.x);
        minY = juce::jmin (minY, p.y);
        maxX = juce::jmax (maxX, p.x);
        maxY = juce::jmax (maxY, p.y);
    }

    float pad = 4.0f;
    minX -= pad; minY -= pad; maxX += pad; maxY += pad;
    float worldW = maxX - minX;
    float worldH = maxY - minY;

    auto area = getLocalBounds().toFloat();
    float scale = juce::jmin (area.getWidth() / worldW, area.getHeight() / worldH);
    float drawnW = worldW * scale;
    float drawnH = worldH * scale;
    float offsetX = area.getX() + (area.getWidth()  - drawnW) * 0.5f;
    float offsetY = area.getY() + (area.getHeight() - drawnH) * 0.5f;

    auto w2s = juce::AffineTransform::scale (scale)
                   .translated (offsetX - minX * scale, offsetY - minY * scale);

    drawTrack    (g, w2s);
    drawSidings  (g, w2s);
    drawSwitches (g, w2s);
    drawCars     (g, w2s);
    drawPlayers  (g, w2s);
}

void GameView::drawTrack (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();

    for (int i = 0; i < track.numSegments(); ++i)
    {
        const auto& seg = track.getSegment (i);
        auto a = track.getNode (seg.nodeA).position;
        auto b = track.getNode (seg.nodeB).position;

        a.applyTransform (w2s);
        b.applyTransform (w2s);

        g.setColour (track.isMainLine (i)
            ? juce::Colour::fromRGB (140, 130, 110)
            : juce::Colour::fromRGB (120, 110, 95));
        g.drawLine (a.x, a.y, b.x, b.y, track.isMainLine (i) ? 6.0f : 4.0f);
    }

    g.setColour (juce::Colour::fromRGB (90, 85, 75));
    for (int i = 0; i < track.numSegments(); ++i)
    {
        const auto& seg = track.getSegment (i);
        auto a = track.getNode (seg.nodeA).position;
        auto b = track.getNode (seg.nodeB).position;
        a.applyTransform (w2s);
        b.applyTransform (w2s);

        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt (dx * dx + dy * dy);
        if (len < 1.0f) continue;
        float nx = -dy / len, ny = dx / len;
        float railGap = track.isMainLine (i) ? 2.0f : 1.5f;

        g.drawLine (a.x + nx * railGap, a.y + ny * railGap,
                    b.x + nx * railGap, b.y + ny * railGap, 1.0f);
        g.drawLine (a.x - nx * railGap, a.y - ny * railGap,
                    b.x - nx * railGap, b.y - ny * railGap, 1.0f);
    }
}

void GameView::drawSwitches (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();

    for (const auto& sw : track.getSwitches())
    {
        auto centre = track.getNode (sw.node).position;
        centre.applyTransform (w2s);

        auto dirTo = [&] (int seg) -> juce::Point<float>
        {
            const auto& s = track.getSegment (seg);
            int other = (s.nodeA == sw.node) ? s.nodeB : s.nodeA;
            auto target = track.getNode (other).position;
            target.applyTransform (w2s);
            auto d = target - centre;
            float len = d.getDistanceFromOrigin();
            return len > 0.0f ? d / len : juce::Point<float>();
        };

        // Normal: stem ↔ normal.  Reversed: normal ↔ reverse.
        int segA = sw.reversed ? sw.normalSegment  : sw.stemSegment;
        int segB = sw.reversed ? sw.reverseSegment : sw.normalSegment;

        auto dA = dirTo (segA);
        auto dB = dirTo (segB);

        bool canToggle = state.canToggleSwitch (sw.node);
        auto colour = canToggle ? juce::Colours::limegreen : juce::Colours::red;

        auto dInactive = dirTo (sw.reversed ? sw.stemSegment : sw.reverseSegment);
        g.setColour (colour.withAlpha (0.25f));
        g.drawLine (centre.x, centre.y,
                    centre.x + dInactive.x * 14.0f, centre.y + dInactive.y * 14.0f, 1.5f);

        g.setColour (colour);
        g.drawLine (centre.x, centre.y,
                    centre.x + dA.x * 14.0f, centre.y + dA.y * 14.0f, 2.5f);
        g.drawLine (centre.x, centre.y,
                    centre.x + dB.x * 14.0f, centre.y + dB.y * 14.0f, 2.5f);
        g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    }
}

void GameView::drawSidings (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();
    const juce::Colour kSlotColours[4] {
        juce::Colour::fromRGB (230,  70,  70),
        juce::Colour::fromRGB ( 70, 140, 230),
        juce::Colour::fromRGB ( 80, 200,  90),
        juce::Colour::fromRGB (240, 200,  60),
    };

    for (const auto& siding : track.getSidings())
    {
        auto bufPos = track.getNode (siding.bufferNode).position;
        bufPos.applyTransform (w2s);

        g.setColour (kSlotColours[siding.playerSlot].withAlpha (0.4f));
        g.fillEllipse (bufPos.x - 12.0f, bufPos.y - 12.0f, 24.0f, 24.0f);

        g.setColour (kSlotColours[siding.playerSlot]);
        g.drawEllipse (bufPos.x - 12.0f, bufPos.y - 12.0f, 24.0f, 24.0f, 2.0f);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("P" + juce::String (siding.playerSlot + 1),
                    (int) (bufPos.x - 10), (int) (bufPos.y - 7), 20, 14,
                    juce::Justification::centred);
    }
}

void GameView::drawCars (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();

    for (const auto& car : state.getCars())
    {
        if (! car.free) continue;

        auto pos = track.worldPos (car.pos);
        pos.applyTransform (w2s);

        float angle = track.trackAngle (car.pos, car.dir);
        float halfW = 10.0f, halfH = 6.0f;

        juce::Path rect;
        rect.addRectangle (-halfW, -halfH, halfW * 2, halfH * 2);
        rect.applyTransform (juce::AffineTransform::rotation (angle)
                                 .translated (pos.x, pos.y));

        g.setColour (juce::Colour::fromRGB (180, 140, 80));
        g.fillPath (rect);
        g.setColour (juce::Colour::fromRGB (100, 80, 50));
        g.strokePath (rect, juce::PathStrokeType (1.5f));
    }
}

void GameView::drawPlayers (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();

    for (const auto& p : state.getPlayers())
    {
        auto drawCars = [&] (const std::vector<int>& list, bool front)
        {
            for (int ci = (int) list.size() - 1; ci >= 0; --ci)
            {
                auto cpos = state.carWorldPos (p, front, ci);
                cpos.applyTransform (w2s);
                float cangle = state.carAngle (p, front, ci);

                float halfW = 10.0f, halfH = 6.0f;
                juce::Path rect;
                rect.addRectangle (-halfW, -halfH, halfW * 2, halfH * 2);
                rect.applyTransform (juce::AffineTransform::rotation (cangle)
                                         .translated (cpos.x, cpos.y));

                g.setColour (p.colour.withAlpha (0.7f));
                g.fillPath (rect);
                g.setColour (p.colour);
                g.strokePath (rect, juce::PathStrokeType (1.5f));
            }
        };

        drawCars (p.frontCars, true);
        drawCars (p.rearCars, false);

        auto locoWorld = track.worldPos (p.pos);
        locoWorld.applyTransform (w2s);
        float locoAngle = track.trackAngle (p.pos, p.dir);

        float halfW = 14.0f, halfH = 7.0f;
        juce::Path loco;
        loco.startNewSubPath (-halfW, -halfH);
        loco.lineTo (halfW - 3.0f, -halfH);
        loco.lineTo (halfW, 0.0f);
        loco.lineTo (halfW - 3.0f, halfH);
        loco.lineTo (-halfW, halfH);
        loco.closeSubPath();
        loco.applyTransform (juce::AffineTransform::rotation (locoAngle)
                                 .translated (locoWorld.x, locoWorld.y));

        g.setColour (p.colour);
        g.fillPath (loco);
        g.setColour (juce::Colours::black);
        g.strokePath (loco, juce::PathStrokeType (1.5f));

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);
        g.drawText ("P" + juce::String (p.slot + 1),
                    (int) (locoWorld.x - 8), (int) (locoWorld.y - 6), 16, 12,
                    juce::Justification::centred);
    }
}

} // namespace view
