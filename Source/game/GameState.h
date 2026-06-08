#pragma once

#include "Track.h"
#include "Player.h"
#include <vector>

namespace gin { class GameControllerManager; }

namespace game
{

class GameState
{
public:
    explicit GameState (int numPlayers);

    void update (float dt, gin::GameControllerManager& controllers);

    const TrackGraph&          getTrack()   const noexcept { return track; }
    TrackGraph&                getTrack()         noexcept { return track; }
    const std::vector<Player>& getPlayers() const noexcept { return players; }
    const std::vector<Car>&    getCars()    const noexcept { return cars; }

    bool  isGameOver()       const noexcept { return gameOver; }
    float getTimeRemaining() const noexcept { return timeRemaining; }

    void spawnPlayer (int controllerIndex, int slot);
    void spawnAi (int slot);

    juce::Point<float> carWorldPos (const Player& p, bool front, int index) const;
    float              carAngle    (const Player& p, bool front, int index) const;

    bool canToggleSwitch (int switchNode) const;

private:
    void  updatePlayer    (Player& p, float moveDist, bool toggle, bool uncouple);
    void  aiUpdate        (Player& p, float dt);
    void  checkCoupling   (Player& p);
    void  checkScoring    (Player& p);
    void     placeInitialCars();
    TrackPos spawnPosNearDropOff (int dropOffIndex) const;
    bool  isSwitchOccupied (int switchNode) const;
    float collisionLimit   (const Player& p, int moveDir, float maxDist) const;

    TrackGraph           track;
    std::vector<Player>  players;
    std::vector<Car>     cars;
    int                  nextCarId   = 0;
    bool                 gameOver    = false;
    float                timeRemaining = 300.0f;

    static constexpr float kTrainSpeed    = 8.0f;
    static constexpr float kCarSpacing    = 0.9f;
    static constexpr float kCoupleDistance = 1.0f;
    static constexpr float kScoreDistance  = 2.5f;
    static constexpr int   kMaxConsist     = 100;
    static constexpr int   kInitialCars    = 20;
};

} // namespace game
