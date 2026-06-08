#include "TrainRenderer.h"
#include "BinaryData.h"

namespace view
{

namespace
{
    std::unique_ptr<juce::Drawable> buildTrainDrawable()
    {
        auto svg = juce::XmlDocument::parse (
            juce::String::fromUTF8 (BinaryData::train_svg,
                                    BinaryData::train_svgSize));

        if (svg == nullptr)
            return {};

        return juce::Drawable::createFromSVG (*svg);
    }
}

void TrainRenderer::draw (juce::Graphics& g,
                          juce::Point<float> position,
                          juce::Colour colour,
                          float worldSize)
{
    static auto drawable = buildTrainDrawable();
    if (drawable == nullptr)
        return;

    auto bounds = drawable->getDrawableBounds();
    if (bounds.isEmpty())
        return;

    float scale = worldSize / juce::jmax (bounds.getWidth(), bounds.getHeight());

    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::translation (-bounds.getCentreX(), -bounds.getCentreY())
                        .scaled (scale)
                        .translated (position.x, position.y));

    drawable->replaceColour (juce::Colours::black, colour);
    drawable->draw (g, 1.0f);
    drawable->replaceColour (colour, juce::Colours::black);
}

} // namespace view
