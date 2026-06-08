#pragma once

#include <JuceHeader.h>

namespace view
{

class TrainRenderer
{
public:
    static void draw (juce::Graphics& g,
                      juce::Point<float> position,
                      juce::Colour colour,
                      float worldSize = 60.0f);
};

} // namespace view
