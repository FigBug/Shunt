#pragma once

#include "../game/GameState.h"
#include <JuceHeader.h>

namespace view
{

class GameView : public juce::Component
{
public:
    explicit GameView (game::GameState& state);

    void paint (juce::Graphics&) override;

private:
    game::GameState& state;

    void drawTrack    (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawSwitches (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawSwitchRings (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawCars     (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawSmoke    (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawPlayers  (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;
    void drawSidings  (juce::Graphics& g, const juce::AffineTransform& w2s, float s) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GameView)
};

} // namespace view
