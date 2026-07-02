#pragma once

#include "Track.h"
#include "Player.h"
#include "Physics.h"
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

    void spawnPlayer (int controllerIndex, int slot);
    void spawnAi (int slot);

    juce::Point<float> carWorldPos (const Player& p, bool front, int index) const;
    float              carAngle    (const Player& p, bool front, int index) const;

    bool canToggleSwitch (int switchNode) const;

private:
    void  handleActions   (Player& p, float engineSpeed, bool toggleFwd, bool toggleBack, bool uncouple);
    float aiUpdate        (Player& p, float dt);
    void  checkCoupling   (Player& p);
    void  checkScoring    (Player& p);
    void  placeInitialCars();
    struct SpawnInfo { TrackPos pos; int dir; };
    SpawnInfo spawnPosNearDropOff (int dropOffIndex) const;
    bool  isSwitchOccupied (int switchNode) const;

    TrackGraph           track;
    PhysicsEngine        physics;
    std::vector<Player>  players;
    std::vector<Car>     cars;
    std::vector<int>     spawnDropOffOrder;
    int                  nextCarId   = 0;
    bool                 gameOver    = false;

    static constexpr float kTrainSpeed    = 8.0f;
    static constexpr float kCarSpacing    = 1.6f;
    static constexpr float kVehicleHalfLen = 0.8f;
    static constexpr float kEngineMass    = 3.0f;
    static constexpr float kCarMass       = 1.0f;
    static constexpr float kCarFriction   = 2.0f;
    static constexpr float kCoupleDistance = 0.5f;
    static constexpr float kScoreDistance  = 2.5f;
    static constexpr int   kMaxConsist     = 100;
    static constexpr int   kInitialCars    = 20;
};

} // namespace game
