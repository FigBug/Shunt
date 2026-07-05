#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include <gin_controllers/gin_controllers.h>

#include "game/GameState.h"
#include "view/GameView.h"
#include "view/Hud.h"
#include "view/TitleScreen.h"
#include "audio/SoundEngine.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    void startGame();
    void returnToTitle();

    juce::ScopedLowPowerModeDisabler        keepAwake;
    gin::GameControllerManager              controllers;
    audio::SoundEngine                      soundEngine;
    std::unique_ptr<view::TitleScreen>      titleScreen;
    std::unique_ptr<game::GameState>        state;
    std::unique_ptr<view::GameView>         gameView;
    std::unique_ptr<view::Hud>              hud;
    double                                  lastTickMs = 0.0;
    bool                                    inGame     = false;
    bool                                    endScreenReady = false;
    int                                     savedNumPlayers = 2;
    int                                     savedMapIndex   = 0;
    std::unique_ptr<juce::PropertiesFile>   settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
