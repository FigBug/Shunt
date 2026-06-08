#pragma once

#include "../game/GameState.h"
#include <JuceHeader.h>

namespace view
{

class Hud : public juce::Component
{
public:
    explicit Hud (const game::GameState& state);

    void paint (juce::Graphics&) override;

private:
    const game::GameState& state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Hud)
};

} // namespace view
