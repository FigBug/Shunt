#include "GameView.h"
#include <cmath>

namespace view
{

namespace
{
    juce::Colour carColourToJuce (game::CarColour c)
    {
        switch (c)
        {
            case game::CarColour::red:    return juce::Colour::fromRGB (220,  60,  60);
            case game::CarColour::blue:   return juce::Colour::fromRGB ( 60, 100, 220);
            case game::CarColour::green:  return juce::Colour::fromRGB ( 50, 180,  70);
            case game::CarColour::yellow: return juce::Colour::fromRGB (220, 200,  50);
            default:                      return juce::Colours::grey;
        }
    }
}

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

        // The "active branch" is the diverging segment currently connected.
        // Normal: stem↔normal (through), reverse disconnected.
        // Reversed: normal↔reverse (branch), stem disconnected.
        // Show a line from centre toward the branch that is currently active.
        auto branchDir = dirTo (sw.reversed ? sw.reverseSegment : sw.stemSegment);

        bool canToggle = state.canToggleSwitch (sw.node);
        auto colour = canToggle ? juce::Colours::limegreen : juce::Colours::red;

        g.setColour (colour);
        g.drawLine (centre.x, centre.y,
                    centre.x + branchDir.x * 14.0f, centre.y + branchDir.y * 14.0f, 2.5f);
        g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    }
}

void GameView::drawSidings (juce::Graphics& g, const juce::AffineTransform& w2s) const
{
    const auto& track = state.getTrack();



    for (const auto& dz : track.getDropOffs())
    {
        auto dzPos = track.getNode (dz.node).position;
        dzPos.applyTransform (w2s);

        auto cc = carColourToJuce ((game::CarColour) dz.colourIndex);
        g.setColour (cc.withAlpha (0.3f));
        g.fillRoundedRectangle (dzPos.x - 14.0f, dzPos.y - 14.0f, 28.0f, 28.0f, 6.0f);
        g.setColour (cc);
        g.drawRoundedRectangle (dzPos.x - 14.0f, dzPos.y - 14.0f, 28.0f, 28.0f, 6.0f, 2.0f);
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

        g.setColour (carColourToJuce (car.colour));
        g.fillPath (rect);
        g.setColour (carColourToJuce (car.colour).darker (0.4f));
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

                juce::Colour cc = juce::Colours::grey;
                int carId = list[(size_t) ci];
                for (const auto& car : state.getCars())
                    if (car.id == carId) { cc = carColourToJuce (car.colour); break; }
                g.setColour (cc);
                g.fillPath (rect);
                g.setColour (cc.darker (0.4f));
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
