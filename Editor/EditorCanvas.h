#pragma once

#include <JuceHeader.h>
#include "LevelDocument.h"

// ============================================================================
// EditorCanvas — the pan/zoom drawing surface where the level is authored.
// ============================================================================

class EditorCanvas : public juce::Component
{
public:
    enum class Tool { select, straight, curve, rect, spawn, dropOff, erase };

    explicit EditorCanvas (LevelDocument& doc);

    void setTool (Tool t);
    Tool getTool() const noexcept       { return tool; }

    void setDropColour (int c)          { dropColour = juce::jlimit (0, 3, c); repaint(); }
    int  getDropColour() const noexcept { return dropColour; }

    void frameAll();                    // fit the whole level in view
    int  weldLooseEnds();               // connect loose track ends / drop-offs; returns count
    std::function<void()> onStatusChanged;   // fired when tool/hover changes
    std::function<void()> onToolChanged;     // fired when the active tool changes
    juce::String statusText;

    // Component
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class SelKind { none, node, segment, control, spawn };

    LevelDocument& doc;
    Tool tool = Tool::select;
    int  dropColour = 0;

    // view transform: screen = world * zoom + pan
    float zoom = 1.0f;
    juce::Point<float> pan { 40.0f, 40.0f };

    // interaction state
    SelKind selKind = SelKind::none;
    int      selId  = -1;         // node id / segment id / spawn index
    int      chainNode = -1;      // in-progress straight/curve chain
    bool     dragging = false;
    bool     panning  = false;
    bool     rectDragging = false;      // rubber-band for the rounded-rect tool
    juce::Point<float> rectStart, rectEnd;
    juce::Point<float> panStart, panOrigin;
    juce::Point<float> lastMouseWorld;
    int      hoverSnapNode = -1;        // node a track-tool click / node-drag will snap to
    bool     hoverSplitValid = false;   // hovering a segment interior (would split it)
    juce::Point<float> hoverSplitPoint;

    float rectRadiusFor (juce::Rectangle<float>) const;

    bool snapGrid = false;
    float gridSize = 30.0f;

    // Snap distance for joining track ends / spawns to tracks (screen pixels).
    static constexpr float kSnapPx = 10.0f;

    // helpers
    juce::AffineTransform toScreen() const;
    juce::Point<float> worldToScreen (juce::Point<float>) const;
    juce::Point<float> screenToWorld (juce::Point<float>) const;
    juce::Point<float> snap (juce::Point<float> world) const;

    int hitNode (juce::Point<float> world, float screenRadius = 10.0f) const;
    int hitSegment (juce::Point<float> world, float screenRadius = 7.0f) const;
    int hitControl (juce::Point<float> world, float screenRadius = 8.0f) const;
    int hitSpawn (juce::Point<float> world, float screenRadius = 10.0f) const;

    // Nearest existing node within kSnapPx (screen), excluding one id (-1 = none).
    int snapNodeAt (juce::Point<float> world, int excludeId) const;
    // Closest point on any track segment; returns false if there are no segments.
    bool nearestSegmentPoint (juce::Point<float> world,
                              juce::Point<float>& outPoint, float& outWorldDist) const;
    // Richer variant: also yields the segment id and parameter t along it,
    // optionally ignoring segments incident to `excludeNode`.
    bool nearestSegment (juce::Point<float> world, int excludeNode,
                         int& outSeg, float& outT,
                         juce::Point<float>& outPoint, float& outWorldDist) const;

    int findOrCreateNode (juce::Point<float> world);
    void updateStatus (juce::Point<float> world);

    void drawGrid (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorCanvas)
};
