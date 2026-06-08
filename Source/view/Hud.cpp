#include "Hud.h"

namespace view
{

Hud::Hud (const game::GameState& s) : state (s)
{
    setInterceptsMouseClicks (false, false);
}

void Hud::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float pad = 8.0f;
    constexpr float barH = 28.0f;
    constexpr float swatch = 14.0f;

    int numP = (int) state.getPlayers().size();
    int freeCars = 0;
    for (const auto& c : state.getCars())
        if (c.free) ++freeCars;
    int totalCoupled = 0;
    for (const auto& pl : state.getPlayers())
        totalCoupled += pl.totalCars();

    float barW = area.getWidth() - pad * 2;
    juce::Rectangle<float> bar { pad, pad, barW, barH };
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 160));
    g.fillRoundedRectangle (bar, 6.0f);

    float x = bar.getX() + pad;
    float cy = bar.getCentreY();

    for (int i = 0; i < numP; ++i)
    {
        const auto& p = state.getPlayers()[(size_t) i];

        g.setColour (p.colour);
        g.fillRoundedRectangle (x, cy - swatch * 0.5f, swatch, swatch, 3.0f);
        x += swatch + 4.0f;

        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);
        juce::String label = (p.controllerIndex >= 0)
            ? ("P" + juce::String (p.slot + 1))
            : "AI";
        g.drawText (label + ": " + juce::String (p.score),
                    (int) x, (int) bar.getY(), 70, (int) barH,
                    juce::Justification::centredLeft);
        x += 74.0f;
    }

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    juce::String infoText = state.isGameOver()
        ? "All delivered!"
        : (juce::String (freeCars + totalCoupled) + " cars left");
    g.drawText (infoText, (int) (bar.getRight() - 120.0f), (int) bar.getY(),
                110, (int) barH, juce::Justification::centredRight);

    if (state.isGameOver())
    {
        int best = -1, bestScore = -1;
        bool tie = false;
        for (size_t i = 0; i < state.getPlayers().size(); ++i)
        {
            if (state.getPlayers()[i].score > bestScore)
            {
                bestScore = state.getPlayers()[i].score;
                best = (int) i;
                tie = false;
            }
            else if (state.getPlayers()[i].score == bestScore)
            {
                tie = true;
            }
        }

        float panW = 300.0f, panH = 80.0f;
        juce::Rectangle<float> over { area.getCentreX() - panW * 0.5f,
                                      area.getCentreY() - panH * 0.5f,
                                      panW, panH };
        g.setColour (juce::Colour::fromRGBA (0, 0, 0, 220));
        g.fillRoundedRectangle (over, 12.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (over, 12.0f, 2.0f);

        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        if (tie)
            g.drawText ("Tie!", over, juce::Justification::centred, false);
        else if (best >= 0)
        {
            g.setColour (state.getPlayers()[(size_t) best].colour);
            g.drawText ("P" + juce::String (best + 1) + " wins!",
                        over, juce::Justification::centred, false);
        }
    }
}

} // namespace view
