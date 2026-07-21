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

    drawTrack    (g, w2s, scale);
    drawSidings  (g, w2s, scale);
    drawSwitches (g, w2s, scale);
    drawCars     (g, w2s, scale);
    drawPlayers  (g, w2s, scale);
    drawSwitchRings (g, w2s, scale);
    drawSmoke    (g, w2s, scale);
}

void GameView::drawSwitchRings (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    const auto& track = state.getTrack();

    // A ring in the owning player's colour around the switch they're lined up
    // to throw: solid when it can be thrown now, half-alpha when blocked.
    for (const auto& p : state.getPlayers())
    {
        if (p.ringSwitch < 0)
            continue;

        auto c = track.getNode (p.ringSwitch).position;
        c.applyTransform (w2s);
        float r = 0.7f * s;
        g.setColour (p.colour.withAlpha (p.ringFlippable ? 1.0f : 0.5f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.16f * s);
    }
}

void GameView::drawSmoke (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    for (const auto& puff : state.getSmoke())
    {
        auto p = puff.pos;
        p.applyTransform (w2s);
        float r = puff.radius * s;
        float a = juce::jlimit (0.0f, 1.0f, puff.alpha) * 0.7f;

        // Sooty grey (darker when the engine was boosting); slightly translucent
        // so trains and track read through it.
        g.setColour (juce::Colour::fromFloatRGBA (puff.grey, puff.grey, puff.grey, a));
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
    }
}

void GameView::drawTrack (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    const auto& track = state.getTrack();

    for (int i = 0; i < track.numSegments(); ++i)
    {
        const auto& seg = track.getSegment (i);
        float thickness = 0.3f * s;

        g.setColour (juce::Colour::fromRGB (130, 120, 105));

        if (seg.polyline.size() >= 2)
        {
            for (size_t j = 0; j + 1 < seg.polyline.size(); ++j)
            {
                auto a = seg.polyline[j];  a.applyTransform (w2s);
                auto b = seg.polyline[j + 1]; b.applyTransform (w2s);
                g.drawLine (a.x, a.y, b.x, b.y, thickness);
            }
        }
        else
        {
            auto a = track.getNode (seg.nodeA).position; a.applyTransform (w2s);
            auto b = track.getNode (seg.nodeB).position; b.applyTransform (w2s);
            g.drawLine (a.x, a.y, b.x, b.y, thickness);
        }
    }
}

void GameView::drawSwitches (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    const auto& track = state.getTrack();

    for (const auto& sw : track.getSwitches())
    {
        auto centre = track.getNode (sw.node).position;

        // Sample a point a short distance along each segment's actual polyline
        auto sampleAlong = [&] (int seg, float dist) -> juce::Point<float>
        {
            const auto& s = track.getSegment (seg);
            bool fromA = (s.nodeA == sw.node);
            float d = fromA ? dist : s.length - dist;
            auto pt = track.worldPos ({ seg, juce::jlimit (0.0f, s.length, d) });
            pt.applyTransform (w2s);
            return pt;
        };

        constexpr float kDotRadius  = 0.3f;
        constexpr float kLegLength  = 1.0f;
        float sampleDist = kDotRadius + kLegLength;

        auto pStem = sampleAlong (sw.stemSegment, sampleDist);
        auto pLeg  = sampleAlong (sw.reversed ? sw.reverseSegment : sw.normalSegment, sampleDist);
        auto pDotStem = sampleAlong (sw.stemSegment, kDotRadius);
        auto pDotLeg  = sampleAlong (sw.reversed ? sw.reverseSegment : sw.normalSegment, kDotRadius);
        auto c = centre;
        c.applyTransform (w2s);

        bool canToggle = state.canToggleSwitch (sw.node);
        auto colour = canToggle ? juce::Colours::limegreen : juce::Colours::red;

        float sw_thick = 0.2f * s;
        float dot_r = kDotRadius * s;
        g.setColour (colour);
        g.drawLine (pDotStem.x, pDotStem.y, pStem.x, pStem.y, sw_thick);
        g.drawLine (pDotLeg.x,  pDotLeg.y,  pLeg.x,  pLeg.y,  sw_thick);
        g.fillEllipse (c.x - dot_r, c.y - dot_r, dot_r * 2, dot_r * 2);
    }
}

void GameView::drawSidings (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    const auto& track = state.getTrack();

    float dzSize = 1.0f * s;
    float dzRound = 0.4f * s;

    for (const auto& dz : track.getDropOffs())
    {
        if (! dz.active)
            continue;   // random mode hides drop-offs that aren't currently live

        auto dzPos = track.getNode (dz.node).position;
        dzPos.applyTransform (w2s);

        auto cc = carColourToJuce ((game::CarColour) dz.colourIndex);
        g.setColour (cc.withAlpha (0.3f));
        g.fillRoundedRectangle (dzPos.x - dzSize, dzPos.y - dzSize,
                                dzSize * 2, dzSize * 2, dzRound);
        g.setColour (cc);
        g.drawRoundedRectangle (dzPos.x - dzSize, dzPos.y - dzSize,
                                dzSize * 2, dzSize * 2, dzRound, 0.15f * s);
    }
}

void GameView::drawCars (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
{
    const auto& track = state.getTrack();

    for (const auto& car : state.getCars())
    {
        if (! car.free) continue;

        auto pos = track.worldPos (car.pos);
        pos.applyTransform (w2s);

        float angle = track.trackAngle (car.pos, car.dir);
        float halfW = 0.7f * s, halfH = 0.4f * s;

        juce::Path rect;
        rect.addRectangle (-halfW, -halfH, halfW * 2, halfH * 2);
        rect.applyTransform (juce::AffineTransform::rotation (angle)
                                 .translated (pos.x, pos.y));

        g.setColour (carColourToJuce (car.colour));
        g.fillPath (rect);
        g.setColour (carColourToJuce (car.colour).darker (0.4f));
        g.strokePath (rect, juce::PathStrokeType (0.1f * s));
    }
}

void GameView::drawPlayers (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const
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

                float halfW = 0.7f * s, halfH = 0.4f * s;
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
                g.strokePath (rect, juce::PathStrokeType (0.1f * s));
            }
        };

        drawCars (p.frontCars, true);
        drawCars (p.rearCars, false);

        auto locoWorld = track.worldPos (p.pos);
        locoWorld.applyTransform (w2s);
        float locoAngle = track.trackAngle (p.pos, p.dir);

        float halfW = 0.7f * s, halfH = 0.4f * s;
        float nose = 0.15f * s;
        juce::Path loco;
        loco.startNewSubPath (-halfW, -halfH);
        loco.lineTo (halfW - nose, -halfH);
        loco.lineTo (halfW, 0.0f);
        loco.lineTo (halfW - nose, halfH);
        loco.lineTo (-halfW, halfH);
        loco.closeSubPath();
        loco.applyTransform (juce::AffineTransform::rotation (locoAngle)
                                 .translated (locoWorld.x, locoWorld.y));

        g.setColour (p.colour);
        g.fillPath (loco);
        g.setColour (juce::Colours::black);
        g.strokePath (loco, juce::PathStrokeType (0.1f * s));

        float fontSize = juce::jmax (6.0f, 0.7f * s);
        g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
        g.setColour (juce::Colours::white);
        float tw = 1.2f * s, th = 0.8f * s;
        g.drawText ("P" + juce::String (p.slot + 1),
                    (int) (locoWorld.x - tw * 0.5f), (int) (locoWorld.y - th * 0.5f),
                    (int) tw, (int) th, juce::Justification::centred);
    }
}

} // namespace view
