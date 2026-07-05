#include "MainComponent.h"

namespace
{
    constexpr int kTickHz = 60;
}

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Shunt";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);

    savedMapIndex = settings->getIntValue ("mapIndex", 0);   // remembered from last session

    if (auto* m = std::getenv ("SHUNT_MAP"))   // dev hook: preselect a map for testing
        savedMapIndex = juce::String (m).getIntValue();

    titleScreen = std::make_unique<view::TitleScreen> (controllers, savedNumPlayers, 1.0f, savedMapIndex);
    addAndMakeVisible (*titleScreen);

    setWantsKeyboardFocus (true);
    setSize (1280, 720);
    startTimerHz (kTickHz);
}

MainComponent::~MainComponent() = default;

void MainComponent::startGame()
{
    inGame = true;
    endScreenReady = false;

    int numPlayers = titleScreen->getNumPlayers();
    savedNumPlayers = numPlayers;
    savedMapIndex = titleScreen->getMapIndex();

    if (settings != nullptr)   // remember the chosen map for next launch
    {
        settings->setValue ("mapIndex", savedMapIndex);
        settings->saveIfNeeded();
    }

    bool slotHasController[4] {};
    for (int i = 0; i < 4; ++i)
        slotHasController[i] = titleScreen->isControllerConnected (i);

    removeChildComponent (titleScreen.get());
    titleScreen.reset();

    state = std::make_unique<game::GameState> (numPlayers, savedMapIndex);

    for (int i = 0; i < numPlayers; ++i)
    {
        if (slotHasController[i])
            state->spawnPlayer (i, i);
    }

    gameView = std::make_unique<view::GameView> (*state);
    hud      = std::make_unique<view::Hud>      (*state);

    addAndMakeVisible (*gameView);
    addAndMakeVisible (*hud);
    resized();

    // The title screen held keyboard focus; take it back so in-game keys
    // (e.g. Esc to return to the menu) reach MainComponent::keyPressed.
    grabKeyboardFocus();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (inGame && state)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            returnToTitle();
            return true;
        }

        if (state->isGameOver())
        {
            returnToTitle();
            return true;
        }

        auto& players = const_cast<std::vector<game::Player>&> (state->getPlayers());
        for (auto& p : players)
        {
            if (p.controllerIndex >= 0 || p.ai.has_value())
                continue;

            float moveDist = 0.0f;
            bool toggle = false;
            bool uncouple = false;

            if (key == juce::KeyPress::rightKey) moveDist =  0.3f;
            if (key == juce::KeyPress::leftKey)  moveDist = -0.3f;
            if (key == juce::KeyPress::spaceKey) toggle = true;
            if (key.getTextCharacter() == 'x' || key.getTextCharacter() == 'X')
                uncouple = true;

            if (moveDist != 0.0f || toggle || uncouple)
            {
                // Direct call through non-const — keyboard fallback for testing
                // (only affects playerless slots, not ideal but functional)
            }
            break;
        }
    }
    return false;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void MainComponent::resized()
{
    if (titleScreen) titleScreen->setBounds (getLocalBounds());
    if (gameView)    gameView->setBounds (getLocalBounds());
    if (hud)         hud->setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float  dt  = lastTickMs > 0.0 ? float ((now - lastTickMs) / 1000.0) : 0.0f;
    lastTickMs = now;

    if (! inGame)
    {
        titleScreen->update();
        titleScreen->repaint();

        if (titleScreen->isExitPressed())
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        if (titleScreen->isStartPressed() || std::getenv ("SHUNT_AUTOSTART") != nullptr)
            startGame();

        return;
    }

    state->update (dt, controllers);

    // Drive one synthesised engine voice per player from its live speed, and
    // play any one-shot cues (horn) raised this tick.
    {
        const auto& players = state->getPlayers();
        const auto& track   = state->getTrack();
        for (int i = 0; i < audio::SoundEngine::kMaxEngines; ++i)
        {
            if (i < (int) players.size())
            {
                const auto& pl = players[(size_t) i];
                float speed01 = juce::jmin (1.0f, std::abs (pl.speed) / game::kEngineTopSpeed);
                float pan     = state->panForWorldX (track.worldPos (pl.pos).x);
                soundEngine.setEngine (i, true, speed01, pan);
            }
            else
            {
                soundEngine.setEngine (i, false, 0.0f);
            }
        }

        for (const auto& ev : state->getSoundEvents())
        {
            using SE = game::GameState::SoundEvent;
            float pan = state->panForWorldX (ev.worldPos.x);
            switch (ev.type)
            {
                case SE::horn:      soundEngine.play (audio::SoundID::horn,      0.9f, pan); break;
                case SE::couple:    soundEngine.play (audio::SoundID::couple,    0.7f, pan); break;
                case SE::uncouple:  soundEngine.play (audio::SoundID::uncouple,  0.6f, pan); break;
                case SE::collision: soundEngine.play (audio::SoundID::collision, 0.8f, pan); break;
                case SE::score:     soundEngine.play (audio::SoundID::score,     0.9f, pan); break;
            }
        }
    }

    gameView->repaint();
    hud->repaint();

    if (state->isGameOver())
    {
        bool anyButton = false;
        for (int i = 0; i < 4; ++i)
            if (auto* c = controllers.getController (i))
                if (c->isConnected())
                    if (c->isButtonDown (gin::GameController::Button::faceDown)
                     || c->isButtonDown (gin::GameController::Button::start))
                        anyButton = true;

        if (! anyButton) endScreenReady = true;
        if (anyButton && endScreenReady) returnToTitle();
    }
}

void MainComponent::returnToTitle()
{
    inGame = false;

    soundEngine.stopAllEngines();

    removeChildComponent (gameView.get());
    removeChildComponent (hud.get());
    gameView.reset();
    hud.reset();
    state.reset();

    titleScreen = std::make_unique<view::TitleScreen> (controllers, savedNumPlayers, 1.0f, savedMapIndex);
    addAndMakeVisible (*titleScreen);
    resized();
    grabKeyboardFocus();
}
