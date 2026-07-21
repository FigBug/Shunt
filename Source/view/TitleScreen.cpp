#include "TitleScreen.h"
#include "TrainRenderer.h"
#include "../game/Maps.h"

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

    const char* kModeNames[] { "Classic", "Random" };
    constexpr int kNumModes = 2;
}

TitleScreen::TitleScreen (gin::GameControllerManager& controllers_,
                          int initialPlayers, float initialVolume, int initialMapIndex,
                          int initialModeIndex)
    : controllers (controllers_),
      numPlayers (juce::jlimit (2, 4, initialPlayers)),
      volume (juce::jlimit (0.0f, 1.0f, initialVolume)),
      mapIndex (juce::jlimit (0, juce::jmax (0, (int) game::getMaps().size() - 1), initialMapIndex)),
      modeIndex (juce::jlimit (0, kNumModes - 1, initialModeIndex))
{
    setWantsKeyboardFocus (true);
    juce::Timer::callAfterDelay (100, [this] { grabKeyboardFocus(); });
}

bool TitleScreen::keyPressed (const juce::KeyPress& key)
{
    const int numMaps = (int) game::getMaps().size();

    if (key == juce::KeyPress::escapeKey)
        exitPressed = true;                    // Esc on the menu quits the game
    else if (key == juce::KeyPress::leftKey)
        numPlayers = juce::jmax (numPlayers - 1, 2);
    else if (key == juce::KeyPress::rightKey)
        numPlayers = juce::jmin (numPlayers + 1, 4);
    else if (key == juce::KeyPress::upKey)
    {
        if (numMaps > 0) mapIndex = (mapIndex - 1 + numMaps) % numMaps;
    }
    else if (key == juce::KeyPress::downKey)
    {
        if (numMaps > 0) mapIndex = (mapIndex + 1) % numMaps;
    }
    else if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
        volume = juce::jmin (1.0f, volume + 0.1f);
    else if (key.getTextCharacter() == '-')
        volume = juce::jmax (0.0f, volume - 0.1f);
    else if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
        modeIndex = (modeIndex + 1) % kNumModes;
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
    bool anyUp    = false;
    bool anyDown  = false;
    bool anyDLeft = false;
    bool anyDRight = false;

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
            if (c->isButtonDown (B::dpadUp))        anyUp = true;
            if (c->isButtonDown (B::dpadDown))      anyDown = true;
            if (c->isButtonDown (B::dpadLeft))      anyDLeft = true;
            if (c->isButtonDown (B::dpadRight))     anyDRight = true;
        }
    }

    // Seed the edge detectors on the first tick so buttons already held when
    // the screen appears must be released before they register.
    if (! primed)
    {
        prevBumperRight = anyRight;
        prevBumperLeft  = anyLeft;
        prevDpadUp      = anyUp;
        prevDpadDown    = anyDown;
        prevDpadLeft    = anyDLeft;
        prevDpadRight   = anyDRight;
        prevStart       = anyStart;
        primed = true;
        return;
    }

    const int numMaps = (int) game::getMaps().size();

    if (anyRight && ! prevBumperRight) numPlayers = juce::jmin (numPlayers + 1, 4);
    if (anyLeft  && ! prevBumperLeft)  numPlayers = juce::jmax (numPlayers - 1, 2);
    if (anyDown  && ! prevDpadDown && numMaps > 0) mapIndex = (mapIndex + 1) % numMaps;
    if (anyUp    && ! prevDpadUp   && numMaps > 0) mapIndex = (mapIndex - 1 + numMaps) % numMaps;
    if (anyDRight && ! prevDpadRight) modeIndex = (modeIndex + 1) % kNumModes;
    if (anyDLeft  && ! prevDpadLeft)  modeIndex = (modeIndex - 1 + kNumModes) % kNumModes;
    if (anyStart && ! prevStart)       startPressed = true;

    prevBumperRight = anyRight;
    prevBumperLeft  = anyLeft;
    prevDpadUp      = anyUp;
    prevDpadDown    = anyDown;
    prevDpadLeft    = anyDLeft;
    prevDpadRight   = anyDRight;
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
                (int) bounds.getX(), (int) (cy * 0.66f),
                (int) bounds.getWidth(), 30,
                juce::Justification::centred);

    const auto& maps = game::getMaps();
    juce::String mapName = (mapIndex >= 0 && mapIndex < (int) maps.size())
                             ? maps[(size_t) mapIndex].name : juce::String();
    g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
    g.setColour (juce::Colour::fromRGB (240, 220, 180));
    g.drawText (juce::String::fromUTF8 ("Map:  \xe2\x97\x82 ") + mapName
                + juce::String::fromUTF8 (" \xe2\x96\xb8"),
                (int) bounds.getX(), (int) (cy * 0.76f),
                (int) bounds.getWidth(), 28,
                juce::Justification::centred);

    juce::String modeName = kModeNames[juce::jlimit (0, kNumModes - 1, modeIndex)];
    g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
    g.setColour (juce::Colour::fromRGB (240, 220, 180));
    g.drawText (juce::String::fromUTF8 ("Mode:  \xe2\x97\x82 ") + modeName
                + juce::String::fromUTF8 (" \xe2\x96\xb8"),
                (int) bounds.getX(), (int) (cy * 0.85f),
                (int) bounds.getWidth(), 28,
                juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    int volPct = (int) std::round (volume * 100.0f);
    g.drawText ("Volume: " + juce::String (volPct) + "%",
                (int) bounds.getX(), (int) (cy * 0.93f),
                (int) bounds.getWidth(), 24,
                juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x90\xe2\x86\x92 / LB-RB  Players")
                + juce::String::fromUTF8 ("    \xe2\x86\x91\xe2\x86\x93 / D-pad  Map")
                + "    M / D-pad \xe2\x97\x82\xe2\x96\xb8  Mode"
                + "    +/-  Volume",
                (int) bounds.getX(), (int) (cy * 1.0f),
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
