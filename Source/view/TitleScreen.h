#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <gin_controllers/gin_controllers.h>

namespace view
{

class TitleScreen : public juce::Component
{
public:
    TitleScreen (gin::GameControllerManager& controllers,
                 int initialPlayers = 2, float initialVolume = 1.0f,
                 int initialMapIndex = 0);

    void paint (juce::Graphics& g) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void update();

    int   getNumPlayers()  const noexcept { return numPlayers; }
    float getVolume()      const noexcept { return volume; }
    int   getMapIndex()    const noexcept { return mapIndex; }
    bool  isStartPressed() const noexcept { return startPressed; }
    bool  isExitPressed()  const noexcept { return exitPressed; }

    bool isControllerConnected (int slot) const noexcept;

private:
    gin::GameControllerManager& controllers;
    int   numPlayers   = 2;
    float volume       = 1.0f;
    int   mapIndex     = 0;
    bool  startPressed = false;
    bool  exitPressed  = false;

    bool primed          = false;   // first update seeds prev* so a held-over
                                    // button (e.g. the one that dismissed the
                                    // end screen) isn't read as a fresh press
    bool prevBumperLeft  = false;
    bool prevBumperRight = false;
    bool prevDpadUp      = false;
    bool prevDpadDown    = false;
    bool prevStart       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitleScreen)
};

} // namespace view
