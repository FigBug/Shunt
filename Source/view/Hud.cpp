#include "Hud.h"
#include <cmath>

namespace view
{

Hud::Hud (const game::GameState& s) : state (s)
{
    setInterceptsMouseClicks (false, false);
}

void Hud::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float pad = 12.0f;

    constexpr float rowH   = 32.0f;
    constexpr float swatch = 18.0f;
    const float panelW = 220.0f;
    const float panelH = pad + rowH * (float) state.getPlayers().size() + pad;

    juce::Rectangle<float> panel { area.getX() + pad, area.getY() + pad, panelW, panelH };
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 160));
    g.fillRoundedRectangle (panel, 6.0f);

    float y = panel.getY() + pad;
    for (size_t i = 0; i < state.getPlayers().size(); ++i)
    {
        const auto& p = state.getPlayers()[i];

        g.setColour (p.colour);
        g.fillRoundedRectangle (panel.getX() + pad,
                                y + (rowH - swatch) * 0.5f,
                                swatch, swatch, 3.0f);

        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);

        juce::String label = (p.controllerIndex >= 0 ? "P" : "AI");
        if (p.controllerIndex >= 0) label += juce::String (p.slot + 1);

        juce::Rectangle<int> textArea {
            (int) (panel.getX() + pad + swatch + 10.0f),
            (int) y,
            (int) (panelW - pad * 2 - swatch - 10.0f),
            (int) rowH };

        g.drawText (label, textArea, juce::Justification::centredLeft, false);

        g.setFont (juce::FontOptions (14.0f));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        juce::String carText = juce::String (p.totalCars()) + " cars";
        g.drawText (carText, textArea, juce::Justification::centred, false);

        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (p.score), textArea,
                    juce::Justification::centredRight, false);

        y += rowH;
    }

    juce::String timeText;
    if (state.isGameOver())
        timeText = "Game Over";
    else
        timeText = "Time: " + juce::String ((int) std::ceil (state.getTimeRemaining())) + "s";

    juce::Rectangle<float> timerPanel {
        area.getRight() - 180.0f - pad, area.getY() + pad, 180.0f, rowH };
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 160));
    g.fillRoundedRectangle (timerPanel, 6.0f);

    g.setFont (juce::FontOptions (16.0f));
    g.setColour (state.getTimeRemaining() < 30.0f && ! state.isGameOver()
                     ? juce::Colours::yellow : juce::Colours::white);
    g.drawText (timeText, timerPanel.toNearestInt().reduced (10, 0),
                juce::Justification::centredRight, false);

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

        float panW = 300.0f, panH2 = 80.0f;
        juce::Rectangle<float> over { area.getCentreX() - panW * 0.5f,
                                      area.getCentreY() - panH2 * 0.5f,
                                      panW, panH2 };
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
