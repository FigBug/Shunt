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
    void  decoupleAll     (Player& p, float engineSpeed);
    float aiUpdate        (Player& p, float dt);
    void  checkCoupling   (Player& p);
    void  checkScoring    (Player& p);
    void  placeInitialCars();
    void  placeCarsFromSpawns();   // used when the level defines explicit spawns
    struct SpawnInfo { TrackPos pos; int dir; };
    SpawnInfo spawnPosNearDropOff (int dropOffIndex) const;
    bool  isSwitchOccupied (int switchNode) const;
    // True when this player's own engine or cars foul the switch node.
    bool  trainFoulsSwitch (const Player& p, int switchNode) const;
    // True when another train shares the mover's segment and sits closer to
    // the switch node — that train has the right to the switch, so the mover
    // may not throw it.
    bool  isSwitchBlockedByCloserTrain (const Player& mover, int switchNode) const;
    // True when any engine other than seeker (or its coupled cars) sits within
    // [lo, hi] on the given segment.
    bool  otherEngineInWindow (const Player& seeker, int segment, float lo, float hi) const;
    // True when another engine sits on the route from seeker to target, i.e.
    // between the AI and the car it wants — a reason to give up on that car.
    bool  engineBlocksTarget (const Player& seeker, TrackPos target) const;
    // Drop-off node for a colour, or -1 if none.
    int   dropOffNodeFor (int colourIndex) const;
    // True when picking up this car would put it on the deliverable side — the
    // engine can push it toward its drop-off rather than drag it away.
    bool  isRightSidePickup (const Player& p, juce::Point<float> carWorld, int colourIndex) const;

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
    // Tractive / braking forces. Acceleration = force / mass, so heavier
    // consists build and shed speed more slowly. Powering under throttle,
    // coasting with no throttle, and braking with reverse throttle each use a
    // different force: coast < power < brake.
    static constexpr float kEngineForce   = 24.0f;
    static constexpr float kCoastForce    = 6.0f;
    static constexpr float kBrakeForce    = 36.0f;
    static constexpr float kCoupleDistance = 0.5f;
    static constexpr float kScoreDistance  = 2.5f;
    static constexpr int   kMaxConsist     = 100;
    static constexpr int   kInitialCars    = 20;
};

} // namespace game
