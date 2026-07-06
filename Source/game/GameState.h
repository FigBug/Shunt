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
    explicit GameState (int numPlayers, int mapIndex = 0);

    void update (float dt, gin::GameControllerManager& controllers);

    // Fire-and-forget audio cues raised during update(), drained by the host
    // each tick (mirrors the Provins pattern). worldPos allows future panning.
    struct SoundEvent
    {
        enum Type { horn, couple, uncouple, collision, score, points };
        Type               type;
        juce::Point<float> worldPos;
    };
    const std::vector<SoundEvent>& getSoundEvents() const noexcept { return soundEvents; }

    const TrackGraph&          getTrack()   const noexcept { return track; }
    TrackGraph&                getTrack()         noexcept { return track; }
    const std::vector<Player>& getPlayers() const noexcept { return players; }
    const std::vector<Car>&    getCars()    const noexcept { return cars; }

    bool  isGameOver()       const noexcept { return gameOver; }

    void spawnPlayer (int controllerIndex, int slot);
    void spawnAi (int slot);

    TrackPos           carTrackPos (const Player& p, bool front, int index) const;
    juce::Point<float> carWorldPos (const Player& p, bool front, int index) const;
    float              carAngle    (const Player& p, bool front, int index) const;

    // Stereo pan in [-1, 1] for a world X, mapped across the track's horizontal
    // extent (matches the on-screen camera framing). Used to place sounds.
    float              panForWorldX (float worldX) const;

    bool canToggleSwitch (int switchNode) const;

private:
    void  handleActions   (Player& p, float engineSpeed, bool toggleFwd, bool toggleBack, bool uncouple);
    void  decoupleAll     (Player& p, float engineSpeed);
    float aiUpdate        (Player& p, float dt);
    void  checkCoupling   (Player& p);
    void  checkScoring    (Player& p);
    void  placeInitialCars();
    void  placeCarsFromSpawns();   // used when the level defines explicit spawns
    // Distance along the track from `start` in `dir` to the nearest switch,
    // capped at `limit`. Used to reject spawn points that sit on top of a switch.
    float distanceToSwitch (TrackPos start, int dir, float limit) const;
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

    TrackGraph           track;
    PhysicsEngine        physics;
    std::vector<Player>  players;
    std::vector<Car>     cars;
    std::vector<SoundEvent> soundEvents;
    std::vector<int>     spawnDropOffOrder;
    int                  nextCarId   = 0;
    bool                 gameOver    = false;
    float                collisionCooldown = 0.0f;   // rate-limits collision cues

    static constexpr float kTrainSpeed    = kEngineTopSpeed;
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
    // Cars delivered within this window of each other count as one run, so the
    // whole consist scores as a scaling combo.
    static constexpr float kDeliveryStreakWindow = 1.5f;
    static constexpr int   kMaxConsist     = 100;
    static constexpr int   kInitialCars    = 20;
};

} // namespace game
