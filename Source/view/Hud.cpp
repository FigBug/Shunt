#include "Hud.h"
#include <algorithm>
#include <vector>

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
        const auto& players = state.getPlayers();
        int n = (int) players.size();

        // Rank players by score, highest first; equal scores share a place.
        std::vector<int> order (n);
        for (int i = 0; i < n; ++i) order[(size_t) i] = i;
        std::stable_sort (order.begin(), order.end(),
                          [&] (int a, int b) { return players[(size_t) a].score
                                                    > players[(size_t) b].score; });

        std::vector<int> rank ((size_t) n);
        for (int k = 0; k < n; ++k)
            rank[(size_t) k] = (k > 0 && players[(size_t) order[(size_t) k]].score
                                          == players[(size_t) order[(size_t) (k - 1)]].score)
                                   ? rank[(size_t) (k - 1)] : k + 1;

        const float titleH = 40.0f;
        const float rowH    = 34.0f;
        const float panW    = 360.0f;
        const float panH    = titleH + rowH * (float) n + 24.0f;

        juce::Rectangle<float> over { area.getCentreX() - panW * 0.5f,
                                      area.getCentreY() - panH * 0.5f, panW, panH };
        g.setColour (juce::Colour::fromRGBA (0, 0, 0, 225));
        g.fillRoundedRectangle (over, 12.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (over, 12.0f, 2.0f);

        auto inner = over.reduced (18.0f, 12.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ("Final Standings", inner.removeFromTop (titleH),
                    juce::Justification::centred, false);

        const juce::Colour gold = juce::Colour::fromRGB (255, 205, 60);

        for (int k = 0; k < n; ++k)
        {
            const auto& p = players[(size_t) order[(size_t) k]];
            bool winner = rank[(size_t) k] == 1;

            auto row = inner.removeFromTop (rowH);

            if (winner)
            {
                g.setColour (gold.withAlpha (0.18f));
                g.fillRoundedRectangle (row, 5.0f);
            }

            auto r = row.reduced (4.0f, 0.0f);
            g.setFont (juce::FontOptions (18.0f, winner ? juce::Font::bold : juce::Font::plain));

            // Place (1 / 2 / 3 …)
            g.setColour (winner ? gold : juce::Colours::white);
            g.drawText (juce::String (rank[(size_t) k]) + ".",
                        r.removeFromLeft (34.0f), juce::Justification::centredLeft, false);

            // Colour swatch
            auto sw = r.removeFromLeft (24.0f);
            g.setColour (p.colour);
            g.fillRoundedRectangle (sw.withSizeKeepingCentre (16.0f, 16.0f), 3.0f);

            // Score (right)
            g.setColour (winner ? gold : juce::Colours::white);
            g.drawText (juce::String (p.score),
                        r.removeFromRight (54.0f), juce::Justification::centredRight, false);

            // "WINNER" tag for first place(s)
            if (winner)
            {
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.setColour (gold);
                g.drawText ("WINNER", r.removeFromRight (72.0f),
                            juce::Justification::centredRight, false);
            }

            // Player label
            juce::String label = "P" + juce::String (p.slot + 1)
                               + (p.controllerIndex < 0 ? " (CPU)" : "");
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (18.0f, winner ? juce::Font::bold : juce::Font::plain));
            g.drawText (label, r, juce::Justification::centredLeft, false);
        }
    }
}

} // namespace view
