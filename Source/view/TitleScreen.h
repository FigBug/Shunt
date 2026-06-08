#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <gin_controllers/gin_controllers.h>

namespace view
{

class TitleScreen : public juce::Component
{
public:
    TitleScreen (gin::GameControllerManager& controllers,
                 int initialPlayers = 2, float initialVolume = 1.0f);

    void paint (juce::Graphics& g) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void update();

    int   getNumPlayers()  const noexcept { return numPlayers; }
    float getVolume()      const noexcept { return volume; }
    bool  isStartPressed() const noexcept { return startPressed; }

    bool isControllerConnected (int slot) const noexcept;

private:
    gin::GameControllerManager& controllers;
    int   numPlayers   = 2;
    float volume       = 1.0f;
    bool  startPressed = false;

    bool prevBumperLeft  = false;
    bool prevBumperRight = false;
    bool prevStart       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitleScreen)
};

} // namespace view
