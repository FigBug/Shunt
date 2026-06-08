#include "TitleScreen.h"
#include "TrainRenderer.h"

#include <cmath>

namespace view
{

namespace
{
    const juce::Colour kSlotColours[4] {
        juce::Colour::fromRGB (255, 140,   0),
        juce::Colour::fromRGB (180,  50, 200),
        juce::Colour::fromRGB (  0, 200, 200),
        juce::Colour::fromRGB (200, 200, 200),
    };
}

TitleScreen::TitleScreen (gin::GameControllerManager& controllers_,
                          int initialPlayers, float initialVolume)
    : controllers (controllers_),
      numPlayers (juce::jlimit (2, 4, initialPlayers)),
      volume (juce::jlimit (0.0f, 1.0f, initialVolume))
{
    setWantsKeyboardFocus (true);
    juce::Timer::callAfterDelay (100, [this] { grabKeyboardFocus(); });
}

bool TitleScreen::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
        numPlayers = juce::jmax (numPlayers - 1, 2);
    else if (key == juce::KeyPress::rightKey)
        numPlayers = juce::jmin (numPlayers + 1, 4);
    else if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
        volume = juce::jmin (1.0f, volume + 0.1f);
    else if (key.getTextCharacter() == '-')
        volume = juce::jmax (0.0f, volume - 0.1f);
    else
        startPressed = true;

    repaint();
    return true;
}

bool TitleScreen::isControllerConnected (int slot) const noexcept
{
    if (auto* c = controllers.getController (slot))
        return c->isConnected();
    return false;
}

void TitleScreen::update()
{
    using B = gin::GameController::Button;

    bool anyStart = false;
    bool anyLeft  = false;
    bool anyRight = false;

    for (int i = 0; i < 4; ++i)
    {
        if (auto* c = controllers.getController (i))
        {
            if (! c->isConnected()) continue;

            if (c->isButtonDown (B::faceDown)
             || c->isButtonDown (B::start))
                anyStart = true;

            if (c->isButtonDown (B::leftShoulder))  anyLeft = true;
            if (c->isButtonDown (B::rightShoulder)) anyRight = true;
        }
    }

    if (anyRight && ! prevBumperRight) numPlayers = juce::jmin (numPlayers + 1, 4);
    if (anyLeft  && ! prevBumperLeft)  numPlayers = juce::jmax (numPlayers - 1, 2);
    if (anyStart && ! prevStart)       startPressed = true;

    prevBumperRight = anyRight;
    prevBumperLeft  = anyLeft;
    prevStart       = anyStart;
}

void TitleScreen::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour::fromRGB (30, 30, 35));

    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();

    g.setColour (juce::Colour::fromRGB (240, 220, 180));
    g.setFont (juce::Font (juce::FontOptions().withHeight (72.0f)).boldened());
    g.drawText ("SHUNT", bounds.removeFromTop (cy * 0.6f), juce::Justification::centredBottom);

    g.setFont (juce::Font (juce::FontOptions().withHeight (24.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (juce::String (numPlayers) + " Players",
                (int) bounds.getX(), (int) (cy * 0.7f),
                (int) bounds.getWidth(), 30,
                juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    int volPct = (int) std::round (volume * 100.0f);
    g.drawText ("Volume: " + juce::String (volPct) + "%",
                (int) bounds.getX(), (int) (cy * 0.82f),
                (int) bounds.getWidth(), 24,
                juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawText (juce::String::fromUTF8 ("LB/RB or \xe2\x86\x90\xe2\x86\x92  Players")
                + "   +/-  Volume",
                (int) bounds.getX(), (int) (cy * 0.90f),
                (int) bounds.getWidth(), 22,
                juce::Justification::centred);

    float trainY    = cy * 1.3f;
    float spacing   = 140.0f;
    float startX    = cx - (float) (numPlayers - 1) * spacing * 0.5f;
    float trainScale = juce::jmin (bounds.getWidth() / 800.0f, 1.0f);
    float trainSize  = 80.0f * trainScale;

    for (int i = 0; i < numPlayers; ++i)
    {
        float mx = startX + (float) i * spacing;
        TrainRenderer::draw (g, { mx, trainY }, kSlotColours[i], trainSize);

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f)));
        juce::String label = isControllerConnected (i) ? ("P" + juce::String (i + 1)) : "AI";
        g.drawText (label, (int) (mx - 30), (int) (trainY + trainSize * 0.6f),
                    60, 24, juce::Justification::centred);
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
    g.drawText ("Press any button to start",
                (int) bounds.getX(), (int) (bounds.getBottom() - 80),
                (int) bounds.getWidth(), 30, juce::Justification::centred);
}

} // namespace view
